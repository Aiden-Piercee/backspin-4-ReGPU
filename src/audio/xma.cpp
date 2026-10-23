// The XMA decoder (APU) as titles drive it: a 320-entry array of 64-byte contexts in physical
// memory, and registers at 0x7FEA0000 the title's own XMA library code reads and writes
// (ContextArrayAddress, and per-context Kick / Lock / Clear bits written with stwbrx). The
// kernel only hands out contexts (XMACreateContext / XMAReleaseContext).
//
// The decoder does what the hardware does to the contexts, without producing sound yet: an
// enabled context consumes its input packets at their real frame counts (the packet header's
// frame count; a frame is 512 samples per channel) and writes that many frames of silence into
// its output ring. Streaming code that waits for input buffers to be used up or output to
// arrive (Top Spin 4's sound engine spins on both) then runs as on the console.
#include <array>
#include <atomic>
#include <mutex>
#include <thread>

#include <windows.h>

#include "kernel/imports.h"
#include "log.h"
#include "memory.h"

namespace
{
constexpr uint32_t kRegisterBase = 0x7FEA0000;
constexpr uint32_t kContextCount = 320, kContextSize = 64;
constexpr uint32_t kRegContextArray = 0x600, kRegKick = 0x650, kRegLock = 0x690, kRegClear = 0x6A0;
constexpr uint32_t kBitsPerPacket = 2048 * 8, kBitsPerPacketHeader = 32;
constexpr uint32_t kBytesPerBlock = 256; // one 128-sample subframe of one channel
constexpr uint32_t kBlocksPerFrame = 4;  // a 512-sample frame, per channel

uint32_t g_array = 0; // guest address of context 0
std::mutex g_mutex;
std::array<bool, kContextCount> g_used{};
struct State
{
    bool enabled = false;
    uint32_t packet = UINT32_MAX; // packet whose frames are being produced (buffer-relative)
    uint32_t framesLeft = 0;
};
std::array<State, kContextCount> g_state{};

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

// Context dwords (bit 0 = least significant):
//   0: input buffer 0 packet count 0-11, loop count 12-19, input 0 valid 20, input 1 valid 21,
//      output block count 22-26, output write offset 27-31
//   1: input buffer 1 packet count 0-11, ..., stereo 29, output valid 31
//   2: input read offset in bits 0-25
//   4: current input buffer 31
//   5, 6, 7: input buffer 0, input buffer 1, output buffer (physical)
//   9: output read offset 0-4
void Clear(uint32_t context)
{
    uint32_t d0 = LoadBE32(context), d1 = LoadBE32(context + 4);
    d0 = SetBits(SetBits(SetBits(d0, 20, 1, 0), 21, 1, 0), 27, 5, 0);
    d1 = SetBits(d1, 31, 1, 0);
    StoreBE32(context, d0);
    StoreBE32(context + 4, d1);
    StoreBE32(context + 36, SetBits(LoadBE32(context + 36), 0, 5, 0));
}

void Work(uint32_t index)
{
    const uint32_t context = g_array + index * kContextSize;
    State& s = g_state[index];
    uint32_t d0 = LoadBE32(context), d1 = LoadBE32(context + 4), d2 = LoadBE32(context + 8), d4 = LoadBE32(context + 16);
    if (!Bits(d1, 31, 1))
        return; // no output buffer
    const uint32_t channels = Bits(d1, 29, 1) ? 2 : 1;
    uint32_t blocks = Bits(d0, 22, 5);
    if (!blocks)
        blocks = 32;
    const uint32_t frameBlocks = kBlocksPerFrame * channels;
    const uint32_t output = PhysToGuest(LoadBE32(context + 28));
    const uint32_t readBlock = Bits(LoadBE32(context + 36), 0, 5) % blocks;
    uint32_t writeBlock = Bits(d0, 27, 5) % blocks;
    // Free space: equal offsets mean an empty ring; a full ring clears output valid.
    uint32_t free = writeBlock == readBlock ? blocks : (readBlock + blocks - writeBlock) % blocks;
    bool changed = false;
    while (free >= frameBlocks)
    {
        uint32_t current = Bits(d4, 31, 1);
        if (!Bits(d0, 20 + current, 1))
        {
            // This buffer is used up: the other one, if the title has filled it.
            if (!Bits(d0, 20 + (current ^ 1), 1))
                break;
            current ^= 1;
            d4 = SetBits(d4, 31, 1, current);
            d2 = SetBits(d2, 0, 26, kBitsPerPacketHeader);
            s.packet = UINT32_MAX;
            changed = true;
        }
        const uint32_t packets = current ? Bits(d1, 0, 12) : Bits(d0, 0, 12);
        const uint32_t input = PhysToGuest(LoadBE32(context + 20 + current * 4));
        const uint32_t packet = Bits(d2, 0, 26) / kBitsPerPacket;
        if (packet >= packets)
        {
            d0 = SetBits(d0, 20 + current, 1, 0);
            s.packet = UINT32_MAX;
            changed = true;
            continue;
        }
        const uint32_t header = LoadBE32(input + packet * 2048);
        if (s.packet != packet)
        {
            s.packet = packet;
            s.framesLeft = header >> 26; // frames that start in this packet
        }
        if (s.framesLeft)
        {
            for (uint32_t b = 0; b < frameBlocks; b++)
                memset(GuestPtr(output + ((writeBlock + b) % blocks) * kBytesPerBlock), 0, kBytesPerBlock);
            writeBlock = (writeBlock + frameBlocks) % blocks;
            free -= frameBlocks;
            s.framesLeft--;
            changed = true;
        }
        if (!s.framesLeft)
        {
            // Next packet of this stream: past the ones interleaved for other streams.
            const uint32_t skip = header & 0xFF;
            const uint32_t next = packet + 1 + (skip == 0xFF ? 0 : skip);
            d2 = SetBits(d2, 0, 26, next * kBitsPerPacket + kBitsPerPacketHeader);
            changed = true;
        }
    }
    if (!changed)
        return;
    d0 = SetBits(d0, 27, 5, writeBlock);
    if (free == 0)
        d1 = SetBits(d1, 31, 1, 0); // full: the title consumes and sets it valid again
    StoreBE32(context, d0);
    StoreBE32(context + 4, d1);
    StoreBE32(context + 8, d2);
    StoreBE32(context + 16, d4);
}

void DecoderLoop()
{
    SetThreadDescription(GetCurrentThread(), L"xma decoder");
    for (;;)
    {
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
                    Clear(g_array + index * kContextSize);
                    g_state[index] = {};
                }
                if (locked >> bit & 1)
                    g_state[index].enabled = false;
                if (kick >> bit & 1)
                    g_state[index].enabled = true;
            }
        }
        for (uint32_t i = 0; i < kContextCount; i++)
            if (g_used[i] && g_state[i].enabled)
                Work(i);
    }
}
}

void XmaInit()
{
    g_array = GuestAlloc(Heap::Physical, kContextCount * kContextSize, 256);
    Register(kRegContextArray) = g_array & 0x1FFFFFFF;
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
        g_state[i] = {};
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
        g_used[index] = false;
        g_state[index] = {};
        memset(GuestPtr(g_array + index * kContextSize), 0, kContextSize);
    }
    Return(ctx, 0);
}
