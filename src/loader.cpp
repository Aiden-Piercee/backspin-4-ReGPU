#include "loader.h"
#include "indirect.h"

#include <fstream>
#include <mutex>
#include <windows.h>
#include <vector>
#include <unordered_map>

#include <ppc_config.h>
#include <ppc_context.h>

#include <image.h>
#ifdef FM2_TITLE_UPDATE
#include <xex_patcher.h>
#endif

#include "log.h"
#include "memory.h"
#include "user_dir.h"

LoadedTitle g_title;
static std::vector<LoadedModule> g_loadedModules;
static std::filesystem::path g_gameTitleDir;

namespace
{
constexpr uint32_t kXexHeaderTls = 0x00020104;
constexpr uint32_t kXexHeaderStackSize = 0x00020200;
constexpr uint32_t kXexHeaderImports = 0x000103FF;
constexpr uint32_t kXexHeaderExecutionId = 0x00040006;
constexpr uint32_t kXexHeaderSystemFlags = 0x00030000;

std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        Fatal("cannot open %s", path.string().c_str());
    return { std::istreambuf_iterator<char>(f), {} };
}

uint32_t Be32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

uint32_t AllocZeroed(uint32_t size)
{
    return GuestAlloc(Heap::Virtual, size, 16);
}

// Guest storage for a kernel variable export, by xboxkrnl ordinal.
// Shared across all loaded XEX/DLL modules in the process.
uint32_t KernelVariable(uint32_t ordinal)
{
    static std::mutex s_mutex;
    static std::unordered_map<uint32_t, uint32_t> s_vars;
    std::lock_guard lock(s_mutex);
    if (auto it = s_vars.find(ordinal); it != s_vars.end())
        return it->second;

    // OBJECT_TYPE: Allocate, Free, Close, Delete, Parse procedures, DefaultObject (+0x14) and
    // PoolTag (+0x18). DefaultObject 0 says the dispatcher header is at the start of the object
    // body, which holds for all of these (waits on an object go through it, see GetGuestObject).
    const auto objectType = [](uint32_t tag) {
        const uint32_t type = AllocZeroed(0x40);
        StoreBE32(type + 0x18, tag);
        return type;
    };
    uint32_t addr = 0;
    switch (ordinal)
    {
    case 0x00E: addr = objectType(0x45766E74); break; // ExEventObjectType 'Evnt'
    case 0x012: addr = objectType(0x4D757461); break; // ExMutantObjectType 'Muta'
    case 0x017: addr = objectType(0x53656D61); break; // ExSemaphoreObjectType 'Sema'
    case 0x01B: addr = objectType(0x54687265); break; // ExThreadObjectType 'Thre'
    case 0x01C: addr = objectType(0x54696D65); break; // ExTimerObjectType 'Time'
    case 0x059: // KeDebugMonitorData
    case 0x266: // KeCertMonitorData
    case 0x1C1: // VdHSIOCalibrationLock
        addr = AllocZeroed(0x100);
        break;
    case 0x0AD: // KeTimeStampBundle: InterruptTime, SystemTime, TickCount
        if (!g_title.keTimeStampBundle)
            g_title.keTimeStampBundle = AllocZeroed(0x18);
        addr = g_title.keTimeStampBundle;
        break;
    case 0x1AE: // ExLoadedCommandLine
    {
        addr = AllocZeroed(0x100);
        strcpy(GuestPtr<char>(addr), "\"default.xex\"");
        break;
    }
    case 0x193: // XexExecutableModuleHandle: pointer to the module handle
    {
        addr = AllocZeroed(4);
        StoreBE32(addr, g_title.ldrEntry);
        break;
    }
    case 0x1C0: // VdGpuClockInMHz
    {
        addr = AllocZeroed(4);
        StoreBE32(addr, 500);
        break;
    }
    case 0x156: // XboxHardwareInfo: Flags, NumberOfProcessors(6), ...
    {
        addr = AllocZeroed(16);
        StoreBE32(addr + 0, 0x00000020); // HDD present
        *GuestPtr<uint8_t>(addr + 4) = 6; // 6 hardware threads
        break;
    }
    case 0x158: // XboxKrnlVersion: major, minor, build, qfe
    {
        addr = AllocZeroed(8);
        auto* v = GuestPtr<uint16_t>(addr);
        v[0] = __builtin_bswap16(2);
        v[1] = __builtin_bswap16(0);
        v[2] = __builtin_bswap16(17559);
        v[3] = 0;
        break;
    }
    case 0x1BE: // VdGlobalDevice
    case 0x1BF: // VdGlobalXamDevice
        addr = AllocZeroed(4);
        break;
    default:
        Log("loader: unknown kernel variable ordinal %03X, giving it zeroed storage", ordinal);
        addr = AllocZeroed(0x100);
        break;
    }
    s_vars[ordinal] = addr;
    return addr;
}
}

uint32_t KernelObjectType(uint32_t ordinal)
{
    return KernelVariable(ordinal);
}

namespace
{

void ResolveVariableImports(const uint8_t* xex, uint32_t textStart, uint32_t textEnd)
{
    const uint32_t importsOffset = [&] {
        const uint32_t count = Be32(xex + 0x14);
        for (uint32_t i = 0; i < count; i++)
        {
            if (Be32(xex + 0x18 + i * 8) == kXexHeaderImports)
                return Be32(xex + 0x18 + i * 8 + 4);
        }
        return 0u;
    }();
    if (!importsOffset)
        return;

    const uint8_t* header = xex + importsOffset;
    const uint32_t stringTableSize = Be32(header + 4);
    const uint32_t libraryCount = Be32(header + 8);
    std::vector<std::string> names;
    for (const char* p = reinterpret_cast<const char*>(header + 12); names.size() < libraryCount;)
    {
        names.emplace_back(p);
        p += ((names.back().size() + 1) + 3) & ~size_t(3);
    }

    const uint8_t* library = header + 12 + stringTableSize;
    for (uint32_t lib = 0; lib < libraryCount; lib++)
    {
        const uint32_t librarySize = Be32(library);
        const uint16_t nameIndex = (library[0x24] << 8) | library[0x25];
        const uint16_t recordCount = (library[0x26] << 8) | library[0x27];
        auto inText = [&](uint32_t a) { return a >= textStart && a < textEnd; };
        for (uint32_t r = 0; r < recordCount; r++)
        {
            const uint32_t record = Be32(library + 0x28 + r * 4);
            if (inText(record))
                continue; // function thunk, replaced by the recompiled import
            if (r + 1 < recordCount)
            {
                // A function import: an address-table slot followed by its
                // thunk. Point the slot at the thunk for indirect calls.
                const uint32_t next = Be32(library + 0x28 + (r + 1) * 4);
                if (inText(next))
                {
                    StoreBE32(record, next);
                    continue;
                }
            }
            // XenonUtils leaves variable slots byte-swapped: host order now.
            const uint32_t value = *GuestPtr<uint32_t>(record);
            const uint32_t ordinal = value & 0xFFFF;
            if (names[nameIndex] != "xboxkrnl.exe")
            {
                Log("loader: variable import %03X from %s not supported", ordinal, names[nameIndex].c_str());
                StoreBE32(record, AllocZeroed(0x100));
                continue;
            }
            StoreBE32(record, KernelVariable(ordinal));
        }
        library += librarySize;
    }
}

uint32_t OptHeader(const uint8_t* xex, uint32_t key, bool* found = nullptr)
{
    const uint32_t count = Be32(xex + 0x14);
    for (uint32_t i = 0; i < count; i++)
    {
        if (Be32(xex + 0x18 + i * 8) == key)
        {
            if (found)
                *found = true;
            return Be32(xex + 0x18 + i * 8 + 4);
        }
    }
    if (found)
        *found = false;
    return 0;
}
}

uint32_t XexHeaderField(uint32_t headerBase, uint32_t key)
{
    const uint8_t* xex = GuestPtr<uint8_t>(headerBase);
    const uint32_t count = Be32(xex + 0x14);
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t entry = 0x18 + i * 8;
        if (Be32(xex + entry) != key)
            continue;
        switch (key & 0xFF)
        {
        case 0x00:
            return Be32(xex + entry + 4);
        case 0x01:
            return headerBase + entry + 4;
        default:
            return headerBase + Be32(xex + entry + 4);
        }
    }
    return 0;
}

const std::filesystem::path& TitleUpdateDir()
{
    static const std::filesystem::path dir = [] {
        char buffer[MAX_PATH];
        if (GetEnvironmentVariableA("FM2_TITLE_UPDATE_DIR", buffer, MAX_PATH))
            return std::filesystem::path(buffer);
        return DocumentsUserDir() / "update";
    }();
    return dir;
}

void LoadTitle(const std::filesystem::path& xexPath)
{
    std::vector<uint8_t> file = ReadFile(xexPath);
#ifdef FM2_TITLE_UPDATE
    {
        // This build's recompiled code is the title-updated executable: the update's patch
        // turns the disc's default.xex into it (the same patch XenonRecomp applied).
        const std::filesystem::path xexp = TitleUpdateDir() / "default.xexp";
        if (!std::filesystem::exists(xexp))
            Fatal("this build needs the title update: %s is missing (tools/stfs.py update <package> <folder>)", xexp.string().c_str());
        const std::vector<uint8_t> patch = ReadFile(xexp);
        std::vector<uint8_t> patched;
        const auto result = XexPatcher::apply(file.data(), file.size(), patch.data(), patch.size(), patched, false);
        if (result != XexPatcher::Result::Success)
            Fatal("could not apply %s to %s (result %d): is it the right title update for this disc?", xexp.string().c_str(),
                xexPath.string().c_str(), int(result));
        file = std::move(patched);
        Log("loader: applied the title update %s (%zu bytes)", xexp.string().c_str(), patch.size());
    }
#endif
    Image image = Image::ParseImage(file.data(), file.size());
    if (!image.data)
        Fatal("%s: could not decrypt/decompress the XEX", xexPath.string().c_str());
    if (image.base != PPC_IMAGE_BASE)
        Fatal("%s: image base %08zX does not match the recompiled code (%08llX); wrong default.xex?", xexPath.string().c_str(), image.base, PPC_IMAGE_BASE);

    // Copy entire decrypted XEX image into guest memory (includes PE headers, sections,
    // and non-PE resources such as CORETECH, LAYOUT, BOOT, XDBF).
    memcpy(GuestPtr(uint32_t(image.base)), image.data.get(), image.size);
    uint32_t textStart = 0, textEnd = 0;
    for (const Section& s : image.sections)
    {
        // .reloc runs past the end of XenonUtils' buffer and is not needed.
        if (s.name == ".reloc")
            continue;
        memcpy(GuestPtr(uint32_t(s.base)), s.data, s.size);
        if (s.name == ".text")
        {
            textStart = uint32_t(s.base);
            textEnd = uint32_t(s.base + s.size);
        }
    }

    // Indirect-call table: host function for every guest code address of every image.
    IndirectInit();

    // XEX header, kept in guest memory for RtlImageXexHeaderField and XAM.
    const uint32_t headerSize = Be32(file.data() + 8);
    g_title.xexHeader = GuestAlloc(Heap::Virtual, headerSize, 0x1000);
    memcpy(GuestPtr(g_title.xexHeader), file.data(), headerSize);

    g_title.entryPoint = uint32_t(image.entry_point);
    g_title.stackSize = OptHeader(file.data(), kXexHeaderStackSize);
    g_title.systemFlags = OptHeader(file.data(), kXexHeaderSystemFlags);
    if (const uint32_t tls = OptHeader(file.data(), kXexHeaderTls))
    {
        g_title.tlsSlotCount = Be32(file.data() + tls);
        g_title.tlsTemplate = Be32(file.data() + tls + 4);
        g_title.tlsDataSize = Be32(file.data() + tls + 8);
    }
    if (const uint32_t exec = OptHeader(file.data(), kXexHeaderExecutionId))
    {
        g_title.executionId = g_title.xexHeader + exec;
        g_title.titleId = Be32(file.data() + exec + 0xC);
    }

    // Module handle: LDR_DATA_TABLE_ENTRY with the fields titles read.
    g_title.ldrEntry = GuestAlloc(Heap::Virtual, 0x100);
    const uint32_t ldr = g_title.ldrEntry;
    for (uint32_t list = 0; list < 0x18; list += 8)
    {
        StoreBE32(ldr + list, ldr + list);
        StoreBE32(ldr + list + 4, ldr + list);
    }
    StoreBE32(ldr + 0x18, uint32_t(image.base));
    StoreBE32(ldr + 0x1C, uint32_t(image.base));
    StoreBE32(ldr + 0x20, image.size);
    StoreBE32(ldr + 0x38, image.size);
    StoreBE32(ldr + 0x3C, g_title.entryPoint);
    StoreBE32(ldr + 0x58, g_title.xexHeader);

    ResolveVariableImports(file.data(), textStart, textEnd);

    Log("loader: %s title %08X entry %08X stack %#x tls %u slots + %#x bytes @%08X", xexPath.string().c_str(),
        g_title.titleId, g_title.entryPoint, g_title.stackSize, g_title.tlsSlotCount, g_title.tlsDataSize, g_title.tlsTemplate);

    g_gameTitleDir = xexPath.parent_path();
    LoadedModule defaultMod;
    defaultMod.name = "default.xex";
    defaultMod.base = uint32_t(image.base);
    defaultMod.size = image.size;
    defaultMod.entryPoint = g_title.entryPoint;
    defaultMod.xexHeader = g_title.xexHeader;
    defaultMod.ldrEntry = g_title.ldrEntry;
    defaultMod.exportTable = 0;
    g_loadedModules.push_back(defaultMod);

    // Preload secondary modules (Loader_DLL.xex and Swing_DLL.xex)
    LoadModuleImage("Loader_DLL.xex");
    LoadModuleImage("Swing_DLL.xex");
}


LoadedModule* FindLoadedModule(uint32_t ldrEntry)
{
    for (auto& mod : g_loadedModules)
    {
        if (mod.ldrEntry == ldrEntry || (ldrEntry != 0 && mod.base == ldrEntry))
            return &mod;
    }
    return nullptr;
}

LoadedModule* FindLoadedModuleByName(const std::string& name)
{
    std::string baseName = name;
    size_t lastSlash = baseName.find_last_of("\\/");
    if (lastSlash != std::string::npos)
        baseName = baseName.substr(lastSlash + 1);

    for (auto& mod : g_loadedModules)
    {
        if (_stricmp(mod.name.c_str(), baseName.c_str()) == 0)
            return &mod;
    }
    return nullptr;
}

void InitModule(LoadedModule& mod, PPCContext& ctx, uint8_t* base)
{
    if (mod.initialized || !mod.entryPoint)
        return;
    mod.initialized = true;
    Log("loader: invoking DllMain for %s at %08X (hModule=%08X)", mod.name.c_str(), mod.entryPoint, mod.ldrEntry);
    PPCHostFunc* func = PPCLookupIndirect(mod.entryPoint);
    if (!func)
    {
        Log("loader: entry point %08X not found in indirect table", mod.entryPoint);
        return;
    }

    PPCContext dllCtx = ctx;
    dllCtx.r3.u64 = mod.ldrEntry;
    dllCtx.r4.u64 = 1; // DLL_PROCESS_ATTACH
    dllCtx.r5.u64 = 0; // lpvReserved
    dllCtx.lr = 0;

    func(dllCtx, base);
    Log("loader: DllMain for %s finished (result %08X)", mod.name.c_str(), dllCtx.r3.u32);
}

uint32_t GetModuleHandleByName(const std::string& name)
{
    if (name.empty())
        return g_title.ldrEntry;
    LoadedModule* mod = FindLoadedModuleByName(name);
    return mod ? mod->ldrEntry : 0;
}

uint32_t LoadModuleImage(const std::string& name)
{
    std::string baseName = name;
    size_t lastSlash = baseName.find_last_of("\\/");
    if (lastSlash != std::string::npos)
        baseName = baseName.substr(lastSlash + 1);

    for (const auto& mod : g_loadedModules)
    {
        if (_stricmp(mod.name.c_str(), baseName.c_str()) == 0)
            return mod.ldrEntry;
    }

    std::filesystem::path fullPath = g_gameTitleDir / baseName;
    if (!std::filesystem::exists(fullPath))
    {
        fullPath = "game" / std::filesystem::path(baseName);
        if (!std::filesystem::exists(fullPath))
        {
            Log("loader: module file %s not found", fullPath.string().c_str());
            return 0;
        }
    }

    std::vector<uint8_t> file = ReadFile(fullPath);
    Image image = Image::ParseImage(file.data(), file.size());
    if (!image.data)
    {
        Log("loader: could not decrypt/decompress %s", fullPath.string().c_str());
        return 0;
    }

    // Copy entire decrypted XEX image into guest memory (includes PE headers, sections,
    // and non-PE resources).
    memcpy(GuestPtr(uint32_t(image.base)), image.data.get(), image.size);
    uint32_t textStart = 0, textEnd = 0;
    for (const Section& s : image.sections)
    {
        if (s.name == ".reloc")
            continue;
        memcpy(GuestPtr(uint32_t(s.base)), s.data, s.size);
        if (s.name == ".text")
        {
            textStart = uint32_t(s.base);
            textEnd = uint32_t(s.base + s.size);
        }
    }

    const uint32_t headerSize = Be32(file.data() + 8);
    uint32_t xexHeader = GuestAlloc(Heap::Virtual, headerSize, 0x1000);
    memcpy(GuestPtr(xexHeader), file.data(), headerSize);

    // Module handle: LDR_DATA_TABLE_ENTRY
    uint32_t ldr = GuestAlloc(Heap::Virtual, 0x100);
    for (uint32_t list = 0; list < 0x18; list += 8)
    {
        StoreBE32(ldr + list, ldr + list);
        StoreBE32(ldr + list + 4, ldr + list);
    }
    StoreBE32(ldr + 0x18, uint32_t(image.base));
    StoreBE32(ldr + 0x1C, uint32_t(image.base));
    StoreBE32(ldr + 0x20, image.size);
    StoreBE32(ldr + 0x38, image.size);
    StoreBE32(ldr + 0x3C, uint32_t(image.entry_point));
    StoreBE32(ldr + 0x58, xexHeader);

    // Read export table from security info
    uint32_t secOffset = Be32(file.data() + 0x10);
    uint32_t exportTable = 0;
    if (secOffset + 0x160 <= file.size())
    {
        exportTable = Be32(file.data() + secOffset + 4 + 4 + 0x100 + 4 + 4 + 4 + 0x14 + 4 + 0x14 + 16 + 16);
    }

    ResolveVariableImports(file.data(), textStart, textEnd);

    LoadedModule mod;
    mod.name = baseName;
    mod.base = uint32_t(image.base);
    mod.size = image.size;
    mod.entryPoint = uint32_t(image.entry_point);
    mod.xexHeader = xexHeader;
    mod.ldrEntry = ldr;
    mod.exportTable = exportTable;
    g_loadedModules.push_back(mod);

    Log("loader: loaded module %s base %08X size %08X entry %08X exportTable %08X",
        baseName.c_str(), mod.base, mod.size, mod.entryPoint, mod.exportTable);
    return ldr;
}

uint32_t GetModuleProcAddress(uint32_t ldrEntry, uint32_t ordinal)
{
    const LoadedModule* mod = nullptr;
    for (const auto& m : g_loadedModules)
    {
        if (m.ldrEntry == ldrEntry)
        {
            mod = &m;
            break;
        }
    }
    if (!mod && (ldrEntry == 0 || ldrEntry == g_title.ldrEntry))
    {
        // default.xex
        return 0;
    }
    if (!mod)
    {
        Log("loader: GetModuleProcAddress: unknown module handle %08X", ldrEntry);
        return 0;
    }

    if (mod->exportTable)
    {
        // xex2_export_table starts at exportTable + 32 (skip 32-byte header)
        const uint32_t tbl = mod->exportTable + 32;
        const uint16_t hibase = LoadBE16(tbl + 2);
        const uint32_t count = LoadBE32(tbl + 4);
        const uint32_t base = LoadBE32(tbl + 8);
        if (ordinal >= base && ordinal < base + count)
        {
            const uint32_t offset = LoadBE32(tbl + 12 + (ordinal - base) * 4);
            const uint32_t addr = (uint32_t(hibase ? hibase : (mod->base >> 16)) << 16) | offset;
            Log("loader: GetModuleProcAddress(%s, ord %u) -> %08X", mod->name.c_str(), ordinal, addr);
            return addr;
        }
    }

    // Specific known fallback ordinals
    if (_stricmp(mod->name.c_str(), "Loader_DLL.xex") == 0)
    {
        if (ordinal == 1) return 0x8348B0E0;
        if (ordinal == 2) return 0x83492C70;
        if (ordinal == 3) return 0x83492920;
    }
    else if (_stricmp(mod->name.c_str(), "Swing_DLL.xex") == 0)
    {
        if (ordinal == 1) return 0x885F5218;
        if (ordinal == 2) return 0x885F5228;
        if (ordinal == 3) return 0x885F5210;
    }

    Log("loader: GetModuleProcAddress(%s, ord %u): ordinal not found", mod->name.c_str(), ordinal);
    return 0;
}