#include <filesystem>
#include <thread>
#include <algorithm>
#include <string>

#include <windows.h>

#include "kernel/runtime.h"
#include "kernel/thread.h"
#include "loader.h"
#include "debug.h"
#include "log.h"
#include "memory.h"
#include "ts4_settings.h"
#include "launcher.h"

void CrashHandlerInit();
void GpuLogState();

int main(int argc, char** argv)
{
    if (HDESK desk = OpenDesktopW(L"Default", 0, FALSE, GENERIC_ALL))
        SetThreadDesktop(desk);

    CrashHandlerInit();

    // Parse command line arguments
    std::filesystem::path gameDir = "game";
    bool explicitGameDir = false;
    bool noLauncherArg = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--no-launcher")
        {
            noLauncherArg = true;
        }
        else if (arg[0] != '-')
        {
            gameDir = arg;
            explicitGameDir = true;
        }
    }

    if (noLauncherArg)
        SetEnvironmentVariableA("TS4_NO_LAUNCHER", "1");

    // Load persisted settings
    LoadTs4Settings();
    Ts4Settings& s = GetTs4Settings();
    if (!explicitGameDir && !s.gameDir.empty())
        gameDir = s.gameDir;

    // Run launcher unless bypassed
    if (!noLauncherArg && !getenv("TS4_NO_LAUNCHER") && !getenv("FM2_NO_LAUNCHER") &&
        !getenv("TS4_TOUR") && !getenv("TS4_HEADLESS"))
    {
        if (!RunLauncher(gameDir))
        {
            Log("topspin4: launcher quit by user");
            return 0;
        }
    }

    // Apply settings to runtime and GPU config
    ApplyTs4SettingsToRuntime();

    Log("topspin4: game folder %s", std::filesystem::absolute(gameDir).string().c_str());

    MemoryInit();
    LoadTitle(gameDir / "default.xex");
    if (const char* dump = getenv("TS4_DUMP_IMAGE"))
    {
        if (FILE* f = fopen(dump, "wb"))
        {
            fwrite(GuestPtr(0x82000000), 1, 0x2000000, f);
            fclose(f);
            Log("wrote the loaded image to %s", dump);
        }
    }
    RuntimeInit(gameDir);
    DebugServerStart();
    ThreadSystemInit(g_title.tlsTemplate, g_title.tlsDataSize, g_title.tlsSlotCount);

    void StartThreadProfiler();
    StartThreadProfiler();
    void StartSpikeRecorder();
    StartSpikeRecorder();

    auto mainThread = CreateGuestThread(g_title.entryPoint, 0, 0, g_title.stackSize, false, "main");
    char watchdogMs[16] = {};
    const DWORD watchdog = GetEnvironmentVariableA("TS4_WATCHDOG", watchdogMs, 16) ? (atoi(watchdogMs) > 1 ? std::max(50, atoi(watchdogMs)) : 3000) : INFINITE;
    while (WaitForSingleObject(mainThread->thread->hostHandle, watchdog) == WAIT_TIMEOUT)
    {
        Log("watchdog:");
        LogGuestThreads();
        GpuLogState();
        void LogFileStats();
        LogFileStats();
    }
    Log("topspin4: main thread exited with %08X", mainThread->exitCode);
    return 0;
}
