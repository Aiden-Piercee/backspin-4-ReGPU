// The XMA decoder (APU) as titles drive it: a 320-entry array of 64-byte contexts in physical
// memory, and registers at 0x7FEA0000 the title's own XMA library code reads and writes
// (ContextArrayAddress, and per-context Kick / Lock / Clear bits written with stwbrx). The
// kernel only hands out contexts (XMACreateContext / XMAReleaseContext).
//
// The hardware works a frame at a time: it reads the frame at the context's input read offset
// (a bit offset into the current input buffer's 2048-byte packets; frames run on across packet
// boundaries, into the stream's next packet), writes its 512 samples per channel to the output
// ring as 16-bit big-endian, channels interleaved, and moves the read offset to the next frame,
// marking an input buffer used up once reading leaves it. Titles start streams at a frame offset
// inside a packet and watch the read offset move, so it moves a frame at a time here too.
//
// Each frame goes to FFmpeg's xma2 decoder (avcodec-62.dll / avutil-60.dll beside the
// executable, loaded at startup) on its own, in a one-frame packet; the decoder keeps the
// stream's overlap between frames. Without FFmpeg (or with TS4_XMA_SILENCE=1) frames are
// written as silence, which keeps the title's streaming running all the same.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

#include "kernel/imports.h"
#include "log.h"
#include "memory.h"
#include "precise_sleep.h"

namespace
{
constexpr uint32_t kRegisterBase = 0x7FEA0000;
constexpr uint32_t kContextCount = 320, kContextSize = 64;
constexpr uint32_t kRegContextArray = 0x600, kRegKick = 0x650, kRegLock = 0x690, kRegClear = 0x6A0;
// The context the decoder is working on, as 0x200 | index (0 when idle). After writing a
// context's Lock bit, a title waits for the decoder to leave that context, then copies it,
// changes the copy and writes it back over the context before the kick: a pass still running
// then (or begun after the lock) had its changes lost, read offsets and all.
constexpr uint32_t kRegCurrentContext = 0x606, kBusy = 0x200;
constexpr uint32_t kBytesPerPacket = 2048, kBitsPerPacket = kBytesPerPacket * 8;
constexpr uint32_t kBytesPerPacketHeader = 4, kBitsPerPacketHeader = 32;
constexpr uint32_t kBytesPerPacketData = kBytesPerPacket - kBytesPerPacketHeader;
constexpr uint32_t kBitsPerFrameHeader = 15, kMaxFrameLength = 0x7FFF;
constexpr uint32_t kMaxFrameBits = kBitsPerPacket - kBitsPerPacketHeader;
constexpr uint32_t kBytesPerBlock = 256;    // one 128-sample subframe of one channel
constexpr uint32_t kSamplesPerFrame = 512;  // per channel
constexpr uint32_t kBlocksPerFrame = 4;     // a 512-sample frame, per channel

// ---- FFmpeg, loaded at run time ----------------------------------------------------------
// Only a handful of functions and fields are used. The fields' offsets (FFmpeg 7/8 layouts)
// are checked against the loaded library before anything is decoded.
namespace av
{
using FindByName = void* (*)(const char*);
using AllocContext = void* (*)(const void*);
using FreeContext = void (*)(void**);
using ParamsAlloc = void* (*)();
using ParamsFree = void (*)(void**);
using ParamsFromContext = int (*)(void*, const void*);
using ParamsToContext = int (*)(void*, const void*);
using Open = int (*)(void*, const void*, void**);
using SendPacket = int (*)(void*, const void*);
using ReceiveFrame = int (*)(void*, void*);
using Flush = void (*)(void*);
using PacketAlloc = void* (*)();
using PacketFree = void (*)(void**);
using NewPacket = int (*)(void*, int);
using PacketUnref = void (*)(void*);
using FrameAlloc = void* (*)();
using FrameFree = void (*)(void**);
using FrameUnref = void (*)(void*);
using Mallocz = void* (*)(size_t);
using LayoutDefault = void (*)(void*, int);
using OptSetInt = int (*)(void*, const char*, int64_t, int);
using OptSet = int (*)(void*, const char*, const char*, int);
using LogSetLevel = void (*)(int);

bool g_ok = false;
const void* g_codec = nullptr;
FindByName findByName;
AllocContext allocContext;
FreeContext freeContext;
ParamsAlloc paramsAlloc;
ParamsFree paramsFree;
ParamsFromContext paramsFromContext;
ParamsToContext paramsToContext;
Open open;
SendPacket sendPacket;
ReceiveFrame receiveFrame;
Flush flush;
PacketAlloc packetAlloc;
PacketFree packetFree;
NewPacket newPacket;
PacketUnref packetUnref;
FrameAlloc frameAlloc;
FrameFree frameFree;
FrameUnref frameUnref;
Mallocz mallocz;
LayoutDefault layoutDefault;
OptSet optSet;

// AVCodecParameters, AVPacket and AVFrame fields.
constexpr size_t kParExtradata = 16, kParExtradataSize = 24, kParChLayout = 128, kParSampleRate = 152, kParBlockAlign = 156;
constexpr size_t kPacketData = 24;
constexpr size_t kFrameNbSamples = 112, kFrameFormat = 116;
constexpr int kSampleFmtFltp = 8, kSampleFmtFlt = 3;

template <typename T>
T& At(void* base, size_t offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }

void Load()
{
    const char* silence = getenv("TS4_XMA_SILENCE");
    if (silence && strcmp(silence, "0") != 0)
        return;
    HMODULE codec = LoadLibraryW(L"avcodec-62.dll"), util = LoadLibraryW(L"avutil-60.dll");
    if (!codec || !util)
    {
        Log("xma: avcodec-62.dll / avutil-60.dll not found beside the executable: XMA audio is silent");
        return;
    }
    bool all = true;
    auto get = [&](HMODULE m, const char* name) {
        void* p = reinterpret_cast<void*>(GetProcAddress(m, name));
        if (!p)
        {
            Log("xma: FFmpeg lacks %s", name);
            all = false;
        }
        return p;
    };
    findByName = FindByName(get(codec, "avcodec_find_decoder_by_name"));
    allocContext = AllocContext(get(codec, "avcodec_alloc_context3"));
    freeContext = FreeContext(get(codec, "avcodec_free_context"));
    paramsAlloc = ParamsAlloc(get(codec, "avcodec_parameters_alloc"));
    paramsFree = ParamsFree(get(codec, "avcodec_parameters_free"));
    paramsFromContext = ParamsFromContext(get(codec, "avcodec_parameters_from_context"));
    paramsToContext = ParamsToContext(get(codec, "avcodec_parameters_to_context"));
    open = Open(get(codec, "avcodec_open2"));
    sendPacket = SendPacket(get(codec, "avcodec_send_packet"));
    receiveFrame = ReceiveFrame(get(codec, "avcodec_receive_frame"));
    flush = Flush(get(codec, "avcodec_flush_buffers"));
    packetAlloc = PacketAlloc(get(codec, "av_packet_alloc"));
    packetFree = PacketFree(get(codec, "av_packet_free"));
    newPacket = NewPacket(get(codec, "av_new_packet"));
    packetUnref = PacketUnref(get(codec, "av_packet_unref"));
    frameAlloc = FrameAlloc(get(util, "av_frame_alloc"));
    frameFree = FrameFree(get(util, "av_frame_free"));
    frameUnref = FrameUnref(get(util, "av_frame_unref"));
    mallocz = Mallocz(get(util, "av_mallocz"));
    layoutDefault = LayoutDefault(get(util, "av_channel_layout_default"));
    auto optSetInt = OptSetInt(get(util, "av_opt_set_int"));
    optSet = OptSet(get(util, "av_opt_set"));
    if (auto level = LogSetLevel(GetProcAddress(util, "av_log_set_level")))
        level(16); // AV_LOG_ERROR
    using LogCallback = void (*)(void*, int, const char*, va_list);
    if (auto setCallback = reinterpret_cast<void (*)(LogCallback)>(GetProcAddress(util, "av_log_set_callback")))
        setCallback([](void*, int level, const char* fmt, va_list args) {
            if (level > 16)
                return;
            char text[512];
            vsnprintf(text, sizeof(text), fmt, args);
            size_t n = strlen(text);
            while (n && (text[n - 1] == '\n' || text[n - 1] == '\r'))
                text[--n] = 0;
            Log("xma: FFmpeg: %s", text);
        });
    if (!all || !(g_codec = findByName("xma2")))
    {
        Log("xma: no usable xma2 decoder in FFmpeg: XMA audio is silent");
        return;
    }
    // The layout check: settings made through AVOptions must show up at the offsets used.
    void* ctx = allocContext(g_codec);
    void* par = paramsAlloc();
    bool layout = ctx && par && optSetInt(ctx, "ar", 44100, 1) >= 0 && optSet(ctx, "ch_layout", "stereo", 1) >= 0 &&
        paramsFromContext(par, ctx) >= 0 && At<int>(par, kParSampleRate) == 44100 && At<int>(par, kParChLayout + 4) == 2 &&
        At<int>(par, 0) == 1 /* AVMEDIA_TYPE_AUDIO */;
    if (par)
        paramsFree(&par);
    if (ctx)
        freeContext(&ctx);
    if (!layout)
    {
        Log("xma: this FFmpeg's structure layout is not the one expected: XMA audio is silent");
        return;
    }
    g_ok = true;
    Log("xma: decoding with FFmpeg's xma2 decoder");
}

// One stream's decoder: mono or stereo at the context's sample rate.
void* OpenDecoder(int channels, int rate)
{
    void* par = paramsAlloc();
    void* ctx = allocContext(g_codec);
    if (!par || !ctx)
        return nullptr;
    paramsFromContext(par, ctx); // the codec type and id
    // XMA2WAVEFORMATEX (34 bytes): the stream count (16 bits) and channel mask (32 bits), little
    // endian. A context decodes one stream.
    uint8_t* extra = static_cast<uint8_t*>(mallocz(34 + 64));
    extra[0] = 1;
    extra[2] = channels == 2 ? 0x3 : 0x4; // front left | right, or front centre
    At<uint8_t*>(par, kParExtradata) = extra;
    At<int>(par, kParExtradataSize) = 34;
    layoutDefault(static_cast<uint8_t*>(par) + kParChLayout, channels);
    At<int>(par, kParSampleRate) = rate;
    At<int>(par, kParBlockAlign) = kBytesPerPacket;
    const int toContext = paramsToContext(ctx, par);
    // Every frame comes out whole (512 samples): the decoder's start delay is not cut from the
    // stream's first frame, as the hardware doesn't either.
    optSet(ctx, "flags2", "+skip_manual", 0);
    const int opened = toContext >= 0 ? open(ctx, g_codec, nullptr) : 0;
    paramsFree(&par);
    if (toContext < 0 || opened < 0)
    {
        Log("xma: decoder setup failed (parameters %d, open %d)", toContext, opened);
        freeContext(&ctx);
        return nullptr;
    }
    return ctx;
}
}

uint32_t g_array = 0; // guest address of context 0
std::mutex g_mutex;
std::array<bool, kContextCount> g_used{};
struct State
{
    bool enabled = false;
    // The decoder and what it was opened for.
    void* decoder = nullptr;
    int channels = 0, rate = 0;
    // The decoded frame being written out (big-endian 16-bit, interleaved), and how far.
    std::array<uint8_t, kSamplesPerFrame * 2 * 2> frame{};
    uint32_t frameBlocks = 0, frameAt = 0;
    // TS4_TRACE_XMA: what happened since the last report. Input buffers used up here and armed
    // (made valid) by the title, passes with no valid input or that filled the output ring.
    uint32_t kicks = 0, works = 0, frames = 0, waits = 0, used = 0, armed = 0, starved = 0, full = 0;
    int peak = 0;
    uint32_t lastValid = 0; // the input valid bits as the last pass saw or left them
    bool music = false;     // its input came from a music file read
};
std::array<State, kContextCount> g_state{};
void* g_packet = nullptr;
void* g_frame = nullptr;
// TS4_TRACE_XMA=1: every few seconds, what was decoded.
uint64_t g_statFrames = 0, g_statFailed = 0, g_statWaits = 0;
int g_statPeak = 0, g_statLastError = 0;
// TS4_XMA_606=rotate: the current-context register only ever shows a rotating index, never a
// busy context, as Xenia (and the ReXGlue build) do. TS4_XMA_CONTINUOUS=1: a kicked context
// stays enabled, and is decoded into whenever it has room, until it is locked or cleared.
// TS4_XMA_POLL_US=n: the decoder looks for kicks every n microseconds instead of every 1 ms.
bool EnvOn(const char* name)
{
    const char* v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}
const bool g_rotate = getenv("TS4_XMA_606") && strcmp(getenv("TS4_XMA_606"), "rotate") == 0;
const bool g_continuous = EnvOn("TS4_XMA_CONTINUOUS");
const int g_pollUs = getenv("TS4_XMA_POLL_US") ? atoi(getenv("TS4_XMA_POLL_US")) : 0;
const bool g_trace = EnvOn("TS4_TRACE_XMA");
// The physical ranges the last few music file reads went to (kernel/imports_io.cpp), so the
// trace can tell the music's context from the effects'.
std::mutex g_musicMutex;
std::array<std::pair<uint32_t, uint32_t>, 8> g_musicReads{};
uint32_t g_musicNext = 0;
bool IsMusicBuffer(uint32_t phys)
{
    phys &= 0x1FFFFFFF;
    std::lock_guard lock(g_musicMutex);
    for (const auto& [start, end] : g_musicReads)
        if (phys - start < end - start)
            return true;
    return false;
}
uint64_t g_traceContexts = 0; // TS4_TRACE_XMA_CONTEXT=n[,n...]: every frame and pass of those contexts (below 64)
bool Traced(uint32_t index) { return index < 64 && (g_traceContexts >> index & 1); }

void ResetState(State& s)
{
    if (s.decoder)
        av::flush(s.decoder);
    s.frameBlocks = s.frameAt = 0;
}

// The title accesses these registers byte-reversed (lwbrx/stwbrx), so they hold host-order
// values; the decoder claims the command bits atomically.
std::atomic<uint32_t>& Register(uint32_t index)
{
    return *reinterpret_cast<std::atomic<uint32_t>*>(GuestPtr(kRegisterBase + index * 4));
}

uint32_t PhysToGuest(uint32_t phys) { return 0xA0000000 + (phys & 0x1FFFFFFF); }

uint32_t Bits(uint32_t v, int shift, int width) { return (v >> shift) & ((1u << width) - 1); }
uint32_t SetBits(uint32_t v, int shift, int width, uint32_t x)
{
    const uint32_t mask = ((1u << width) - 1) << shift;
    return (v & ~mask) | ((x << shift) & mask);
}

// Bit streams are most significant bit first.
uint32_t PeekBits(const uint8_t* p, uint32_t at, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++, at++)
        v = v << 1 | (p[at >> 3] >> (7 - (at & 7)) & 1);
    return v;
}
void CopyBits(uint8_t* to, uint32_t toAt, const uint8_t* from, uint32_t fromAt, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++, toAt++, fromAt++)
    {
        const uint8_t mask = uint8_t(0x80 >> (toAt & 7));
        if (from[fromAt >> 3] >> (7 - (fromAt & 7)) & 1)
            to[toAt >> 3] |= mask;
        else
            to[toAt >> 3] &= ~mask;
    }
}

// Packet header: frames starting in the packet 31-26, first frame's bit offset after the header
// 25-11, metadata 10-8, packets of other streams to skip before this stream's next 7-0.
uint32_t FirstFrameOffset(uint32_t header) { return Bits(header, 11, 15) + kBitsPerPacketHeader; }

// Context dwords (bit 0 = least significant):
//   0: input buffer 0 packet count 0-11, loop count 12-19, input 0 valid 20, input 1 valid 21,
//      output block count 22-26, output write offset 27-31
//   1: input buffer 1 packet count 0-11, subframe decode count 20-23, output padding 24-26,
//      sample rate 27-28, stereo 29, output valid 31
//   2: input read offset in bits 0-25, error status 26-30
//   3: loop start 0-25
//   4: loop end 0-25, current input buffer 31
//   5, 6, 7: input buffer 0, input buffer 1, output buffer (physical)
//   9: output read offset 0-4
struct Fields
{
    uint32_t d[5];
    bool Valid(uint32_t b) const { return Bits(d[0], 20 + b, 1); }
    uint32_t Current() const { return Bits(d[4], 31, 1); }
    uint32_t Read() const { return Bits(d[2], 0, 26); }
    void SetRead(uint32_t v) { d[2] = SetBits(d[2], 0, 26, v); }
    uint32_t Packets(uint32_t b) const { return b ? Bits(d[1], 0, 12) : Bits(d[0], 0, 12); }
    // The current buffer is used up: the title may refill it, and reading goes on in the other.
    void Swap()
    {
        d[0] = SetBits(d[0], 20 + Current(), 1, 0);
        d[4] = SetBits(d[4], 31, 1, Current() ^ 1);
        SetRead(kBitsPerPacketHeader);
    }
};

void Clear(uint32_t context)
{
    uint32_t d0 = LoadBE32(context), d1 = LoadBE32(context + 4);
    d0 = SetBits(SetBits(SetBits(d0, 20, 1, 0), 21, 1, 0), 27, 5, 0);
    d1 = SetBits(d1, 31, 1, 0);
    StoreBE32(context, d0);
    StoreBE32(context + 4, d1);
    StoreBE32(context + 8, SetBits(LoadBE32(context + 8), 0, 26, kBitsPerPacketHeader));
    StoreBE32(context + 36, SetBits(LoadBE32(context + 36), 0, 5, 0));
}

// Decodes one frame (its bits, at the start of a one-frame packet) into s.frame. A frame the
// decoder fails on comes out silent: the hardware's output keeps time.
void DecodeFrame(State& s, const uint8_t* bits, uint32_t at, uint32_t length)
{
    const uint32_t bytes = kSamplesPerFrame * 2 * s.channels;
    memset(s.frame.data(), 0, bytes);
    s.frameBlocks = kBlocksPerFrame * s.channels;
    s.frameAt = 0;
    g_statFrames++;
    s.frames++;
    if (!s.decoder || av::newPacket(g_packet, int(kBytesPerPacket)) < 0)
        return;
    uint8_t* packet = av::At<uint8_t*>(g_packet, av::kPacketData);
    memset(packet, 0, kBytesPerPacket);
    // One frame, starting right after the header; no more frames follow it in the packet.
    packet[0] = 1 << 2;
    packet[2] = 1; // XMA2 packet
    CopyBits(packet, kBitsPerPacketHeader, bits, at, length);
    packet[(kBitsPerPacketHeader + length - 1) >> 3] &= uint8_t(~(0x80 >> ((kBitsPerPacketHeader + length - 1) & 7)));
    const int sent = av::sendPacket(s.decoder, g_packet);
    av::packetUnref(g_packet);
    if (sent < 0)
    {
        g_statFailed++, g_statLastError = sent;
        return;
    }
    for (;;)
    {
        const int got = av::receiveFrame(s.decoder, g_frame);
        if (got < 0)
        {
            if (got != -11 /* EAGAIN */)
                g_statFailed++, g_statLastError = got;
            break;
        }
        const int samples = std::min<int>(av::At<int>(g_frame, av::kFrameNbSamples), kSamplesPerFrame);
        const int format = av::At<int>(g_frame, av::kFrameFormat);
        const float* const* planes = reinterpret_cast<const float* const*>(g_frame); // AVFrame::data
        if (samples > 0 && (format == av::kSampleFmtFltp || format == av::kSampleFmtFlt))
        {
            // A short frame (trimmed at the stream's start) ends where a whole one would.
            uint8_t* out = s.frame.data() + size_t(kSamplesPerFrame - samples) * s.channels * 2;
            for (int i = 0; i < samples; i++)
                for (int c = 0; c < s.channels; c++)
                {
                    const float v = format == av::kSampleFmtFltp ? planes[c][i] : planes[0][i * s.channels + c];
                    const int x = int(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
                    g_statPeak = std::max(g_statPeak, std::abs(x));
                    s.peak = std::max(s.peak, std::abs(x));
                    *out++ = uint8_t(x >> 8);
                    *out++ = uint8_t(x);
                }
        }
        av::frameUnref(g_frame);
    }
    // TS4_XMA_DUMP=<file>: the first decoding context's samples, raw (16-bit big-endian).
    static FILE* dump = getenv("TS4_XMA_DUMP") ? fopen(getenv("TS4_XMA_DUMP"), "wb") : nullptr;
    static const State* dumped = &s;
    if (dump && dumped == &s)
        fwrite(s.frame.data(), 1, bytes, dump), fflush(dump);
}

// The first frame of a packet of this buffer from packet `index` on, as a read offset; 0 if no
// later packet in the buffer starts one.
uint32_t NextPacketFrame(uint32_t input, uint32_t index, uint32_t packets)
{
    for (; index < packets; index++)
    {
        const uint32_t offset = FirstFrameOffset(LoadBE32(input + index * kBytesPerPacket));
        if (offset - kBitsPerPacketHeader < kMaxFrameBits)
            return index * kBitsPerPacket + offset;
    }
    return 0;
}

// Reads the frame at the read offset into s.frame and moves the read offset past it. False if
// there is no frame to read yet (no input, or a frame running on into input not given yet).
bool NextFrame(uint32_t index, State& s, Fields& f)
{
    for (int tries = 0; tries < 8; tries++)
    {
        if (!f.Valid(0) && !f.Valid(1))
            return false;
        if (!f.Valid(f.Current()))
            f.d[4] = SetBits(f.d[4], 31, 1, f.Current() ^ 1), f.SetRead(kBitsPerPacketHeader);
        const uint32_t current = f.Current(), packets = f.Packets(current);
        const uint32_t input = PhysToGuest(LoadBE32(g_array + index * kContextSize + 20 + current * 4));
        if (f.Read() < kBitsPerPacketHeader)
            f.SetRead(kBitsPerPacketHeader);
        const uint32_t packet = f.Read() / kBitsPerPacket;
        if (packet >= packets)
        {
            f.Swap();
            continue;
        }
        const uint32_t packetAddress = input + packet * kBytesPerPacket;
        const uint32_t header = LoadBE32(packetAddress), first = FirstFrameOffset(header);
        uint32_t at = f.Read() % kBitsPerPacket;
        const uint32_t skip = Bits(header, 0, 8);
        if (skip == 0xFF || first - kBitsPerPacketHeader >= kMaxFrameBits)
        {
            // No frame starts in this packet.
            if (const uint32_t next = NextPacketFrame(input, packet + 1, packets))
                f.SetRead(next);
            else
                f.Swap();
            continue;
        }
        if (at < first)
            at = first, f.SetRead(packet * kBitsPerPacket + at);
        // The stream's next packet, where a frame running past this packet's end goes on.
        const uint32_t nextIndex = packet + 1 + skip;
        uint32_t nextAddress = 0;
        if (nextIndex < packets)
            nextAddress = input + nextIndex * kBytesPerPacket;
        else if (f.Valid(current ^ 1) && f.Packets(current ^ 1))
            nextAddress = PhysToGuest(LoadBE32(g_array + index * kContextSize + 20 + (current ^ 1) * 4));
        uint8_t data[kBytesPerPacketData * 2] = {};
        memcpy(data, GuestPtr(packetAddress + kBytesPerPacketHeader), kBytesPerPacketData);
        if (nextAddress)
            memcpy(data + kBytesPerPacketData, GuestPtr(nextAddress + kBytesPerPacketHeader), kBytesPerPacketData);
        const uint32_t bit = at - kBitsPerPacketHeader;
        if (bit + kBitsPerFrameHeader > kMaxFrameBits && !nextAddress)
        {
            g_statWaits++;
            s.waits++;
            return false;
        }
        const uint32_t length = PeekBits(data, bit, kBitsPerFrameHeader);
        if (length <= kBitsPerFrameHeader || length == kMaxFrameLength || length > kMaxFrameBits)
        {
            // Not a frame: the end of this packet's frames.
            if (const uint32_t next = NextPacketFrame(input, nextIndex, packets))
                f.SetRead(next);
            else
                f.Swap();
            continue;
        }
        const uint32_t end = bit + length;
        if (end > kMaxFrameBits && !nextAddress)
        {
            g_statWaits++;
            s.waits++;
            return false;
        }
        DecodeFrame(s, data, bit, length);
        const bool more = PeekBits(data, end - 1, 1) && end < kMaxFrameBits;
        if (Traced(index))
            Log("xma: ctx %u frame buf %u packet %u/%u header %08X at %u length %u%s", index, current, packet, packets, header, at,
                length, more ? "" : " (last in packet)");
        // A hardware loop: after the frame at the loop end, back to the loop start.
        const uint32_t loops = Bits(f.d[0], 12, 8);
        if (loops && f.Read() == std::max(kBitsPerPacketHeader, Bits(f.d[4], 0, 26)))
        {
            f.SetRead(std::max(kBitsPerPacketHeader, Bits(f.d[3], 0, 26)));
            if (loops < 255)
                f.d[0] = SetBits(f.d[0], 12, 8, loops - 1);
            return true;
        }
        if (more)
            f.SetRead(packet * kBitsPerPacket + kBitsPerPacketHeader + end);
        else if (const uint32_t next = NextPacketFrame(input, nextIndex, packets))
            f.SetRead(next);
        else
        {
            // Reading goes on in the other buffer, at its first frame.
            f.Swap();
            if (f.Valid(f.Current()) && f.Packets(f.Current()))
            {
                const uint32_t other = PhysToGuest(LoadBE32(g_array + index * kContextSize + 20 + f.Current() * 4));
                const uint32_t offset = FirstFrameOffset(LoadBE32(other));
                if (offset - kBitsPerPacketHeader < kMaxFrameBits)
                    f.SetRead(offset);
            }
        }
        return true;
    }
    return false;
}

void Work(uint32_t index)
{
    const uint32_t context = g_array + index * kContextSize;
    State& s = g_state[index];
    Fields f;
    for (int i = 0; i < 5; i++)
        f.d[i] = LoadBE32(context + i * 4);
    const Fields start = f;
    const uint32_t valid = Bits(f.d[0], 20, 2);
    s.armed += __builtin_popcount(valid & ~s.lastValid);
    s.lastValid = valid;
    if (!valid)
        s.starved++;
    if (!s.music && (IsMusicBuffer(LoadBE32(context + 20)) || IsMusicBuffer(LoadBE32(context + 24))))
    {
        s.music = true;
        if (g_trace)
            Log("xma: ctx %u plays music", index);
    }
    if (!Bits(f.d[1], 31, 1))
        return; // no output buffer
    const int channels = Bits(f.d[1], 29, 1) ? 2 : 1;
    static const int kRates[4] = { 24000, 32000, 44100, 48000 };
    const int rate = kRates[Bits(f.d[1], 27, 2)];
    if (s.channels != channels || s.rate != rate)
    {
        if (s.decoder)
            av::freeContext(&s.decoder);
        s.decoder = av::g_ok ? av::OpenDecoder(channels, rate) : nullptr;
        s.channels = channels;
        s.rate = rate;
        s.frameBlocks = s.frameAt = 0;
        if (av::g_ok && !s.decoder)
            Log("xma: context %u: the decoder did not open (%d channels, %d Hz)", index, channels, rate);
    }
    const uint32_t blocks = Bits(f.d[0], 22, 5);
    if (!blocks)
        return;
    const uint32_t output = PhysToGuest(LoadBE32(context + 28));
    const uint32_t readBlock = Bits(LoadBE32(context + 36), 0, 5) % blocks;
    uint32_t writeBlock = Bits(f.d[0], 27, 5) % blocks;
    // Room: equal offsets are an empty ring (valid) or a full one (output valid cleared).
    uint32_t room = writeBlock == readBlock ? blocks : (readBlock + blocks - writeBlock) % blocks;
    // The hardware writes subframe_decode_count blocks at a time, and only with room for them
    // and output_buffer_padding blocks besides; the padding is kept free after each frame.
    const uint32_t step = std::max(1u, Bits(f.d[1], 20, 4)), padding = Bits(f.d[1], 24, 3);
    if (room < step + padding)
        return;

    while (room >= step + padding)
    {
        if (s.frameAt == s.frameBlocks && !NextFrame(index, s, f))
            break;
        const uint32_t n = std::min(step, s.frameBlocks - s.frameAt);
        for (uint32_t b = 0; b < n; b++)
        {
            memcpy(GuestPtr(output + writeBlock * kBytesPerBlock), s.frame.data() + (s.frameAt + b) * kBytesPerBlock, kBytesPerBlock);
            writeBlock = (writeBlock + 1) % blocks;
        }
        s.frameAt += n;
        room -= n;
        if (s.frameAt == s.frameBlocks)
            room -= std::min(room, padding);
    }
    // Merged into the context as it is now: the title updates it while this runs (refilling
    // input buffers, consuming output), and only the decoder's own fields change here.
    uint32_t now0 = LoadBE32(context), now1 = LoadBE32(context + 4);
    for (uint32_t b = 0; b < 2; b++)
        if (start.Valid(b) && !f.Valid(b))
            now0 = SetBits(now0, 20 + b, 1, 0); // used up here
    now0 = SetBits(now0, 12, 8, Bits(f.d[0], 12, 8));
    now0 = SetBits(now0, 27, 5, writeBlock);
    if (writeBlock == readBlock)
        now1 = SetBits(now1, 31, 1, 0), s.full++; // full (or nothing to decode into an empty ring)
    s.used += __builtin_popcount(valid & ~Bits(f.d[0], 20, 2));
    s.lastValid = Bits(now0, 20, 2);
    StoreBE32(context, now0);
    StoreBE32(context + 4, now1);
    StoreBE32(context + 8, SetBits(LoadBE32(context + 8), 0, 26, f.Read()));
    StoreBE32(context + 16, SetBits(LoadBE32(context + 16), 31, 1, f.Current()));
    if (Traced(index))
        Log("xma: ctx %u pass: input valid %u%u -> %u%u, current %u, read %u -> %u, ring w%u r%u (of %u) -> w%u, output valid %u", index,
            start.Valid(0), start.Valid(1), Bits(now0, 20, 1), Bits(now0, 21, 1), f.Current(), start.Read(), f.Read(),
            Bits(start.d[0], 27, 5), readBlock, blocks, writeBlock, Bits(now1, 31, 1));
}

void DecoderLoop()
{
    SetThreadDescription(GetCurrentThread(), L"xma decoder");
    Log("xma: current-context register %s, %s, polled every %d us", g_rotate ? "rotating" : "busy while decoding",
        g_continuous ? "kicked contexts stay enabled" : "one pass per kick", g_pollUs > 0 ? g_pollUs : 1000);
    uint32_t rotating = 1;
    for (;;)
    {
        if (g_pollUs > 0)
            PreciseSleepUntil(std::chrono::steady_clock::now() + std::chrono::microseconds(g_pollUs));
        else
            Sleep(1);
        std::lock_guard lock(g_mutex);
        for (uint32_t group = 0; group < kContextCount / 32; group++)
        {
            const uint32_t clear = Register(kRegClear + group).exchange(0);
            const uint32_t locked = Register(kRegLock + group).exchange(0);
            const uint32_t kick = Register(kRegKick + group).exchange(0);
            for (uint32_t bit = 0; bit < 32; bit++)
            {
                const uint32_t index = group * 32 + bit;
                if (clear >> bit & 1)
                {
                    if (g_trace && g_state[index].music)
                        Log("xma: ctx %u (music) cleared", index);
                    Clear(g_array + index * kContextSize);
                    ResetState(g_state[index]);
                    g_state[index].enabled = false;
                }
                if (locked >> bit & 1)
                    g_state[index].enabled = false;
                if (kick >> bit & 1)
                    g_state[index].enabled = true, g_state[index].kicks++;
            }
        }
        for (uint32_t i = 0; i < kContextCount; i++)
            if (g_used[i] && g_state[i].enabled)
            {
                // A kick runs the context once (as Xenia does): the title kicks it again on its next
                // audio pass, after consuming output and refilling input. Busy first, then the
                // lock checked again: a title locking it now waits for the pass to end, and one
                // that locked it already has its copy taken and gets the context as it was.
                if (!g_continuous)
                    g_state[i].enabled = false;
                if (!g_rotate)
                    Register(kRegCurrentContext).store(kBusy | i);
                if (Register(kRegLock + i / 32).load() >> (i % 32) & 1)
                    continue;
                g_state[i].works++;
                Work(i);
            }
        Register(kRegCurrentContext).store(g_rotate ? (rotating = (rotating + 1) % kContextCount) : 0);
        const bool trace = g_trace;
        static auto lastStat = std::chrono::steady_clock::now();
        if (trace && std::chrono::steady_clock::now() - lastStat > std::chrono::seconds(3))
        {
            lastStat = std::chrono::steady_clock::now();
            uint32_t active = 0;
            for (uint32_t i = 0; i < kContextCount; i++)
                active += g_used[i];
            Log("xma: %u active contexts, %llu frames (%llu failed, last error %d), peak %d, %llu waits for input", active,
                (unsigned long long)g_statFrames, (unsigned long long)g_statFailed, g_statLastError, g_statPeak,
                (unsigned long long)g_statWaits);
            // Every music context, and the stereo ones that ran; the rest only count above.
            for (uint32_t i = 0; i < kContextCount; i++)
            {
                State& st = g_state[i];
                const uint32_t c = g_array + i * kContextSize;
                const uint32_t d0 = LoadBE32(c), d1 = LoadBE32(c + 4), d2 = LoadBE32(c + 8), d4 = LoadBE32(c + 16);
                if (g_used[i] && (st.music || (Bits(d1, 29, 1) && (st.kicks || st.works))))
                    Log("xma:   ctx %u%s: kicks %u passes %u frames %u peak %d | buffers used %u armed %u, waits %u, starved %u, full %u | "
                        "in0 %u pkts v%u, in1 %u pkts v%u, cur %u, read %u, out w%u r%u of %u v%u, %s",
                        i, st.music ? " MUSIC" : "", st.kicks, st.works, st.frames, st.peak, st.used, st.armed, st.waits, st.starved, st.full,
                        Bits(d0, 0, 12), Bits(d0, 20, 1), Bits(d1, 0, 12), Bits(d0, 21, 1), Bits(d4, 31, 1), Bits(d2, 0, 26), Bits(d0, 27, 5),
                        Bits(LoadBE32(c + 36), 0, 5), Bits(d0, 22, 5), Bits(d1, 31, 1), Bits(d1, 29, 1) ? "stereo" : "mono");
                st.kicks = st.works = st.frames = st.waits = st.used = st.armed = st.starved = st.full = 0;
                st.peak = 0;
            }
            g_statFrames = g_statFailed = g_statWaits = 0;
            g_statPeak = 0;
        }
    }
}
}

void XmaInit()
{
    g_array = GuestAlloc(Heap::Physical, kContextCount * kContextSize, 256);
    Register(kRegContextArray) = g_array & 0x1FFFFFFF;
    for (const char* trace = getenv("TS4_TRACE_XMA_CONTEXT"); trace && *trace;)
    {
        char* end = nullptr;
        const unsigned long n = strtoul(trace, &end, 10);
        if (end == trace)
            break;
        if (n < 64)
            g_traceContexts |= 1ull << n;
        trace = *end ? end + 1 : end;
    }
    av::Load();
    if (av::g_ok)
    {
        g_packet = av::packetAlloc();
        g_frame = av::frameAlloc();
        av::g_ok = g_packet && g_frame;
    }
    std::thread(DecoderLoop).detach();
    Log("xma: %u contexts at %08X", kContextCount, g_array);
}

GUEST_EXPORT(XMACreateContext)
{
    // (context*)
    std::lock_guard lock(g_mutex);
    for (uint32_t i = 0; i < kContextCount; i++)
    {
        if (g_used[i])
            continue;
        g_used[i] = true;
        g_state[i].music = false;
        ResetState(g_state[i]);
        g_state[i].enabled = false;
        memset(GuestPtr(g_array + i * kContextSize), 0, kContextSize);
        StoreBE32(ctx.r3.u32, g_array + i * kContextSize);
        Return(ctx, X_STATUS_SUCCESS);
        return;
    }
    Return(ctx, X_STATUS_NO_MEMORY);
}

GUEST_EXPORT(XMAReleaseContext)
{
    // (context)
    std::lock_guard lock(g_mutex);
    const uint32_t index = ((ctx.r3.u32 & 0x1FFFFFFF) - (g_array & 0x1FFFFFFF)) / kContextSize;
    if (index < kContextCount)
    {
        if (g_trace && g_state[index].music)
            Log("xma: ctx %u (music) released", index);
        g_state[index].music = false;
        g_used[index] = false;
        ResetState(g_state[index]);
        g_state[index].enabled = false;
        memset(GuestPtr(g_array + index * kContextSize), 0, kContextSize);
    }
    Return(ctx, 0);
}


// A music file read (kernel/imports_io.cpp) went to these bytes.
void AudioNoteMusicRead(const std::filesystem::path& file, uint64_t offset, const uint8_t* data, uint32_t bytes)
{
    const uint32_t start = GuestAddr(data) & 0x1FFFFFFF;
    {
        std::lock_guard lock(g_musicMutex);
        g_musicReads[g_musicNext++ % g_musicReads.size()] = { start, start + bytes };
    }
    if (g_trace)
        Log("xma: music read %s offset %llu, %u bytes -> %08X", file.filename().string().c_str(), (unsigned long long)offset, bytes, GuestAddr(data));
}
