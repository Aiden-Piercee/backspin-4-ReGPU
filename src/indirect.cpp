// The indirect-call table shared by all recompiled images (see ppc_hooks.h). It lives in
// host memory outside the guest's 4 GB, so module images and their tables cannot overlap.
#include "indirect.h"

#include <array>
#include <atomic>
#include <utility>

#include <windows.h>

#include "ppc_hooks.h"
#include <ppc_config.h>
#include <ppc_context.h>

#include "log.h"
#include "memory.h"
#include "recomp_modules.h"

PPCHostFunc** g_indirectTable = nullptr;

// FM2_TRACE_CALLS=addr[,addr...] (hex, up to 8): calls through the table to those guest
// functions are logged, with the caller and the result. Direct calls between recompiled functions don't pass the table.
namespace
{
constexpr int kTraceSlots = 8;
uint32_t g_tracedAddress[kTraceSlots];
PPCHostFunc* g_tracedHost[kTraceSlots];
std::atomic<uint32_t> g_tracedCount[kTraceSlots];

template <int N>
void TracedCall(PPCContext& ctx, uint8_t* base)
{
    const uint32_t lr = uint32_t(ctx.lr), r3 = ctx.r3.u32, r4 = ctx.r4.u32, r5 = ctx.r5.u32;
    g_tracedHost[N](ctx, base);
    const uint32_t n = ++g_tracedCount[N];
    // The first calls, then one every quarter second (sooner for another object).
    static std::atomic<int64_t> last{ 0 };
    static std::atomic<uint32_t> lastObject{ 0 };
    const int64_t now = int64_t(GetTickCount64());
    // (and every call whose result differs from the one before)
    static std::atomic<uint32_t> lastResult{ 0 };
    if (n > 16 && now - last <= 250 && (lastObject == r3 || now - last <= 20) && lastResult == ctx.r3.u32)
        return;
    lastResult = ctx.r3.u32;
    last = now, lastObject = r3;
    // The callers further up: the back chain from r1 (return addresses at frame - 8).
    char chain[200] = "";
    for (uint32_t frame = ctx.r1.u32, i = 0, at = 0; i < 8 && frame && (frame & 3) == 0; i++)
    {
        const uint32_t next = LoadBE32(frame);
        if (next <= frame || next - frame > 0x100000)
            break;
        at += snprintf(chain + at, sizeof(chain) - at, " %08X", LoadBE32(next - 8));
        frame = next;
    }
    Log("call %08X #%u from %08X (r3 %08X r4 %08X r5 %08X) -> %08X, thread %lu; callers%s", g_tracedAddress[N], n, lr, r3, r4, r5,
        ctx.r3.u32, GetCurrentThreadId(), chain);
}

template <int... N>
constexpr std::array<PPCHostFunc*, sizeof...(N)> TracedCalls(std::integer_sequence<int, N...>)
{
    return { &TracedCall<N>... };
}

void TraceCalls(PPCHostFunc** table)
{
    const char* list = getenv("FM2_TRACE_CALLS");
    static constexpr auto calls = TracedCalls(std::make_integer_sequence<int, kTraceSlots>{});
    for (int slot = 0; list && *list && slot < kTraceSlots;)
    {
        char* end = nullptr;
        const uint32_t address = uint32_t(strtoul(list, &end, 16));
        if (end == list)
            break;
        list = *end ? end + 1 : end;
        PPCHostFunc*& entry = table[(address - kIndirectLow) / 4];
        if (address < kIndirectLow || address >= kIndirectHigh || !entry)
            continue;
        g_tracedAddress[slot] = address;
        g_tracedHost[slot] = entry;
        entry = calls[slot++];
        Log("indirect calls: tracing calls to %08X", address);
    }
}
}

void PPCIndirectMiss(uint32_t address)
{
    Fatal("indirect call to %08X: no recompiled function there", address);
}

void IndirectInit()
{
    // Reserve one pointer per instruction over the whole range, then commit only the
    // pages that cover each image's code.
    const size_t entries = (kIndirectHigh - kIndirectLow) / 4;
    auto* table = static_cast<PPCHostFunc**>(VirtualAlloc(nullptr, entries * sizeof(PPCHostFunc*), MEM_RESERVE, PAGE_NOACCESS));
    if (!table)
        Fatal("indirect calls: reserving the table failed (error %lu)", GetLastError());
    size_t mapped = 0;
    for (size_t i = 0; i < g_recompiledModuleCount; i++)
    {
        const RecompiledModule& m = g_recompiledModules[i];
        if (m.codeBase < kIndirectLow || m.codeBase + m.codeSize > kIndirectHigh || m.codeSize == 0)
            Fatal("indirect calls: %s code %08X+%X is outside %08X-%08X", m.guestPath, m.codeBase, m.codeSize, kIndirectLow, kIndirectHigh);
        PPCHostFunc** first = table + (m.codeBase - kIndirectLow) / 4;
        if (!VirtualAlloc(first, size_t(m.codeSize / 4) * sizeof(PPCHostFunc*), MEM_COMMIT, PAGE_READWRITE))
            Fatal("indirect calls: committing the table for %s failed (error %lu)", m.guestPath, GetLastError());
        size_t count = 0;
        for (const PPCFuncMapping* f = m.mappings; f->host; f++, count++)
        {
            if (f->guest < m.codeBase || f->guest >= m.codeBase + m.codeSize)
                Fatal("indirect calls: %s maps %08llX outside its code", m.guestPath, uint64_t(f->guest));
            table[(f->guest - kIndirectLow) / 4] = f->host;
        }
        Log("indirect calls: %s, %zu functions", m.guestPath, count);
        mapped += count;
    }
    TraceCalls(table);
    g_indirectTable = table;
    Log("indirect calls: %zu functions in %zu images", mapped, g_recompiledModuleCount);
}

PPCHostFunc* IndirectFind(uint32_t address)
{
    // Only image code has committed table pages; the hot path (PPCLookupIndirect) skips
    // this check, so a call to any other address faults in the table instead.
    for (size_t i = 0; i < g_recompiledModuleCount; i++)
    {
        const RecompiledModule& m = g_recompiledModules[i];
        if (address - m.codeBase < m.codeSize)
            return g_indirectTable[(address - kIndirectLow) / 4];
    }
    return nullptr;
}
