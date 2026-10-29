#pragma once
#include <cstdint>
#include <filesystem>

struct LoadedTitle
{
    uint32_t entryPoint = 0;
    uint32_t stackSize = 0;
    uint32_t xexHeader = 0;   // guest copy of the XEX header
    uint32_t ldrEntry = 0;    // LDR_DATA_TABLE_ENTRY (the module handle)
    uint32_t tlsTemplate = 0;
    uint32_t tlsDataSize = 0;
    uint32_t tlsSlotCount = 0;
    uint32_t systemFlags = 0;
    uint32_t titleId = 0;
    uint32_t executionId = 0; // guest pointer to XEX_EXECUTION_ID
    uint32_t keTimeStampBundle = 0;
};

extern LoadedTitle g_title;

// Loads default.xex into guest memory and wires up imports. A build for the title update
// (FM2_TITLE_UPDATE) applies the update's default.xexp to it first.
void LoadTitle(const std::filesystem::path& xexPath);

// Where the title update lives (the console's update: device): default.xexp, Media.zip.
// FM2_TITLE_UPDATE_DIR overrides <user>\fm2\update.
const std::filesystem::path& TitleUpdateDir();
// The guest OBJECT_TYPE an xboxkrnl Ex*ObjectType variable export points to, by ordinal
// (0x00E event, 0x012 mutant, 0x017 semaphore, 0x01B thread, 0x01C timer).
uint32_t KernelObjectType(uint32_t ordinal);
// Optional header value (low byte 0x00/0x01) or guest pointer to its data.
uint32_t XexHeaderField(uint32_t headerBase, uint32_t key);

struct LoadedModule
{
    std::string name;
    uint32_t base = 0;
    uint32_t size = 0;
    uint32_t entryPoint = 0;
    uint32_t xexHeader = 0;
    uint32_t ldrEntry = 0;
    uint32_t exportTable = 0;
    bool initialized = false;
};

struct PPCContext;
uint32_t LoadModuleImage(const std::string& name);
LoadedModule* FindLoadedModule(uint32_t ldrEntry);
LoadedModule* FindLoadedModuleByName(const std::string& name);
uint32_t GetModuleHandleByName(const std::string& name);
uint32_t GetModuleProcAddress(uint32_t ldrEntry, uint32_t ordinal);
void InitModule(LoadedModule& mod, PPCContext& ctx, uint8_t* base);