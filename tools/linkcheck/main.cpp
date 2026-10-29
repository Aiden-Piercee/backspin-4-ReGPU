// Link check for the recompiled Top Spin 4 images: every image's code, the import stubs
// and the image table link into one program. Running it checks the table: each
// mapping is inside its image's code range, no guest address is mapped twice, and
// every image's code fits the shared indirect-call table.
#include <cstdio>
#include <unordered_set>

#include <ppc_context.h>

#include "recomp_modules.h"

// The runtime's hooks for the recompiled code (src/ppc_hooks.h); never called here.
void PPCMsrChanged(PPCContext&, uint64_t)
{
}
PPCHostFunc** g_indirectTable = nullptr;
void PPCIndirectMiss(uint32_t)
{
    __builtin_trap();
}
extern "C++" uint64_t Fm2Timebase()
{
    return 0;
}

int main()
{
    std::unordered_set<uint32_t> seen;
    int errors = 0;
    for (size_t i = 0; i < g_recompiledModuleCount; i++)
    {
        const RecompiledModule& m = g_recompiledModules[i];
        // The shared indirect-call table (src/indirect.cpp) covers only this range.
        if (m.codeBase < kIndirectLow || m.codeBase + uint64_t(m.codeSize) > kIndirectHigh)
        {
            std::printf("%s: code %08X+%X is outside the indirect-call range %08X-%08X\n", m.guestPath, m.codeBase, m.codeSize,
                kIndirectLow, kIndirectHigh);
            errors++;
        }
        size_t count = 0;
        for (const PPCFuncMapping* f = m.mappings; f->host; f++, count++)
        {
            const uint32_t guest = uint32_t(f->guest);
            if (guest - m.codeBase >= m.codeSize)
            {
                std::printf("%s: %08X is outside the code range %08X+%X\n", m.guestPath, guest, m.codeBase, m.codeSize);
                errors++;
            }
            if (!seen.insert(guest).second)
            {
                std::printf("%s: %08X is mapped twice\n", m.guestPath, guest);
                errors++;
            }
        }
        std::printf("%s: %zu functions mapped in code range %08X-%08X: ok\n", m.guestPath, count, m.codeBase, m.codeBase + m.codeSize);
    }
    if (errors)
    {
        std::printf("link check: %d errors\n", errors);
        return 1;
    }
    std::printf("link check: ok (%zu unique functions across %zu modules)\n", seen.size(), g_recompiledModuleCount);
    return 0;
}
