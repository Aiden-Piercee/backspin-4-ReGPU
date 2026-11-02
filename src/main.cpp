#include <filesystem>
#include <thread>
#include <algorithm>
#include <string>
#include <atomic>
#include <cstdio>

#include <windows.h>

#include "kernel/runtime.h"
#include "kernel/thread.h"
#include "loader.h"
#include "debug.h"
#include "log.h"
#include "memory.h"
#include "ts4_settings.h"
#include "launcher.h"
#include "launcher_import.h"
#include "version.h"

void CrashHandlerInit();
void GpuLogState();

int main(int argc, char** argv)
{
    if (HDESK desk = OpenDesktopW(L"Default", 0, FALSE, GENERIC_ALL))
        SetThreadDesktop(desk);

    CrashHandlerInit();
    Log("topspin4: version %s (%s)", TS4_VERSION, TS4_GIT);

    // --import <iso> <folder>: extract an Xbox 360 disc image without any window, for TS4Launcher.
    // Prints "PROGRESS <permille> <file>", then "DONE" or "ERROR <text>" (one line each) and exits.
    if (argc >= 4 && std::string(argv[1]) == "--import")
    {
        std::atomic<bool> cancel = false;
        std::string error;
        int last = -1;
        const bool ok = ExtractXdvdfsIso(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]), cancel,
            [&](const ExtractProgress& prog) {
                const int permille = int(prog.fraction * 1000.0f);
                if (permille == last) return;
                last = permille;
                std::string file(prog.currentFile.begin(), prog.currentFile.end());
                printf("PROGRESS %d %s\n", permille, file.c_str());
                fflush(stdout);
            }, error);
        if (ok && error.empty())
            printf("DONE\n");
        else
            printf("ERROR %s\n", error.empty() ? "extraction failed" : error.c_str());
        fflush(stdout);
        return ok && error.empty() ? 0 : 1;
    }

    // Parse command line arguments
    std::filesystem::path gameDir = "game";
    bool explicitGameDir = false;
    bool noLauncherArg = false;
    bool setupOnly = false; // --setup: the game's own window (import, patches), then exit; used by the TS4Launcher app

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--no-launcher")
        {
            noLauncherArg = true;
        }
        else if (arg == "--setup")
        {
            setupOnly = true;
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

    if (setupOnly)
    {
        RunLauncher(gameDir);
        return 0;
    }

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
