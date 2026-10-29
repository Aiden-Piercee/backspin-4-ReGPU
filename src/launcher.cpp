#include "launcher.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "launcher_import.h"
#include "launcher_patch.h"
#include "log.h"
#include "ts4_settings.h"

#pragma comment(lib, "comctl32.lib")

namespace
{
enum : int
{
    kProfile = 100,
    kResolution,
    kAspect,
    kDisplay,
    kMsaa,
    kUpscaler,
    kSkipIntro,
    kFastGeometry,
    kStatusLabel,
    kImportBtn,
    kPatchBtn,
    kPlayBtn,
    kQuitBtn
};

const std::pair<uint32_t, uint32_t> kAspects[] = { { 0, 0 }, { 16, 9 }, { 21, 9 }, { 32, 9 } };
const wchar_t* kAspectNames[] = { L"Auto (match resolution)", L"16:9", L"21:9", L"32:9" };

const uint32_t kMsaaSamples[] = { 1, 2, 4 };
const wchar_t* kMsaaNames[] = { L"Off (1x)", L"2x MSAA", L"4x MSAA" };

const wchar_t* kUpscalerNames[] = { L"Off", L"AMD FSR 1.0" };

struct State
{
    HWND window = nullptr;
    HWND profile = nullptr;
    HWND resolution = nullptr;
    HWND aspect = nullptr;
    HWND display = nullptr;
    HWND msaa = nullptr;
    HWND upscaler = nullptr;
    HWND skipIntro = nullptr;
    HWND fastGeometry = nullptr;
    HWND statusLabel = nullptr;
    HWND importBtn = nullptr;
    HWND patchBtn = nullptr;
    HWND playBtn = nullptr;
    HWND quitBtn = nullptr;

    std::filesystem::path gameDir;
    std::vector<std::pair<uint32_t, uint32_t>> resolutions;
    bool play = false;
    bool done = false;
    bool gameFilesValid = false;
} g_state;

int S(int v) { return MulDiv(v, int(GetDpiForSystem()), 96); }

std::vector<std::pair<uint32_t, uint32_t>> Resolutions(const Ts4Settings& s)
{
    std::set<std::pair<uint32_t, uint32_t>> set{ { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 }, { 3440, 1440 }, { 3840, 2160 }, { s.width, s.height } };
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &mode); i++)
    {
        if (mode.dmPelsWidth >= 1280 && mode.dmPelsHeight >= 720)
            set.insert({ mode.dmPelsWidth, mode.dmPelsHeight });
    }
    std::vector<std::pair<uint32_t, uint32_t>> list(set.begin(), set.end());
    std::sort(list.begin(), list.end(), [](auto a, auto b) { return uint64_t(a.first) * a.second < uint64_t(b.first) * b.second; });
    return list;
}

HWND Label(HWND parent, const wchar_t* text, int x, int y, int w, int h, HFONT font)
{
    HWND ctrl = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, S(x), S(y + 3), S(w), S(h), parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ctrl, WM_SETFONT, WPARAM(font), TRUE);
    return ctrl;
}

HWND Control(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font)
{
    HWND ctrl = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, S(x), S(y), S(w), S(h), parent, HMENU(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ctrl, WM_SETFONT, WPARAM(font), TRUE);
    return ctrl;
}

void UpdateGameStatus()
{
    std::string report;
    g_state.gameFilesValid = ValidateGameDir(g_state.gameDir, &report);

    std::wstring text;
    if (g_state.gameFilesValid)
    {
        text = L"Game Status: Ready (" + g_state.gameDir.wstring() + L")";
    }
    else
    {
        text = L"Game Status: Missing files (" + g_state.gameDir.wstring() + L" - click Import)";
    }

    SetWindowTextW(g_state.statusLabel, text.c_str());
    EnableWindow(g_state.playBtn, g_state.gameFilesValid ? TRUE : FALSE);
}

void ApplySettingsFromUi()
{
    Ts4Settings& s = GetTs4Settings();

    wchar_t name[32] = {};
    GetWindowTextW(g_state.profile, name, 16);
    std::string profile;
    for (const wchar_t* p = name; *p; p++)
    {
        if (*p >= 32 && *p < 127)
            profile += char(*p);
    }
    s.profile = profile.empty() ? "Player" : profile;

    const LRESULT r = SendMessageW(g_state.resolution, CB_GETCURSEL, 0, 0);
    if (r >= 0 && size_t(r) < g_state.resolutions.size())
        std::tie(s.width, s.height) = g_state.resolutions[size_t(r)];

    const LRESULT a = SendMessageW(g_state.aspect, CB_GETCURSEL, 0, 0);
    if (a >= 0 && size_t(a) < std::size(kAspects))
        std::tie(s.aspectW, s.aspectH) = kAspects[size_t(a)];

    s.fullscreen = (SendMessageW(g_state.display, CB_GETCURSEL, 0, 0) == 1);

    const LRESULT m = SendMessageW(g_state.msaa, CB_GETCURSEL, 0, 0);
    if (m >= 0 && size_t(m) < std::size(kMsaaSamples))
        s.msaa = kMsaaSamples[size_t(m)];

    const LRESULT u = SendMessageW(g_state.upscaler, CB_GETCURSEL, 0, 0);
    s.upscaler = (u == 1) ? Ts4Settings::Upscaler::Fsr1 : Ts4Settings::Upscaler::Off;

    s.skipIntro = (SendMessageW(g_state.skipIntro, BM_GETCHECK, 0, 0) == BST_CHECKED);
    s.fastGeometry = (SendMessageW(g_state.fastGeometry, BM_GETCHECK, 0, 0) == BST_CHECKED);
    s.gameDir = g_state.gameDir;
}

// Extraction progress modal dialog
void DoExtractIso(HWND parent, const std::filesystem::path& isoPath, const std::filesystem::path& destDir)
{
    std::atomic<bool> cancelFlag = false;
    std::atomic<bool> doneFlag = false;
    std::string extractError;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefDlgProcW;
    wc.cbWndExtra = DLGWINDOWEXTRA;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = HBRUSH(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ts4_extract_dlg";
    RegisterClassW(&wc);

    RECT r{ 0, 0, S(420), S(180) };
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ts4_extract_dlg", L"Extracting ISO...",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
        r.right - r.left, r.bottom - r.top, parent, nullptr, wc.hInstance, nullptr);

    EnableWindow(parent, FALSE);

    HFONT font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HWND lblFile = CreateWindowW(L"STATIC", L"Preparing extraction...", WS_CHILD | WS_VISIBLE, S(20), S(20), S(380), S(22), dlg, nullptr, wc.hInstance, nullptr);
    SendMessageW(lblFile, WM_SETFONT, WPARAM(font), TRUE);

    HWND pbar = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, S(20), S(50), S(380), S(24), dlg, nullptr, wc.hInstance, nullptr);
    SendMessageW(pbar, PBM_SETRANGE32, 0, 1000);

    HWND btnCancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, S(160), S(95), S(100), S(28), dlg, HMENU(IDCANCEL), wc.hInstance, nullptr);
    SendMessageW(btnCancel, WM_SETFONT, WPARAM(font), TRUE);

    std::thread worker([&]() {
        ExtractXdvdfsIso(isoPath, destDir, cancelFlag, [&](const ExtractProgress& prog) {
            PostMessageW(pbar, PBM_SETPOS, WPARAM(int(prog.fraction * 1000.0f)), 0);
            if (!prog.currentFile.empty())
            {
                std::wstring status = L"Extracting: " + prog.currentFile;
                SetWindowTextW(lblFile, status.c_str());
            }
        }, extractError);
        doneFlag.store(true);
    });

    MSG msg;
    while (!doneFlag.load())
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_COMMAND && LOWORD(msg.wParam) == IDCANCEL)
            {
                cancelFlag.store(true);
                SetWindowTextW(lblFile, L"Cancelling extraction...");
                EnableWindow(btnCancel, FALSE);
            }
            if (msg.message == WM_CLOSE && msg.hwnd == dlg)
            {
                cancelFlag.store(true);
                SetWindowTextW(lblFile, L"Cancelling extraction...");
                EnableWindow(btnCancel, FALSE);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(15);
    }

    if (worker.joinable())
        worker.join();

    EnableWindow(parent, TRUE);
    DestroyWindow(dlg);
    DeleteObject(font);

    if (cancelFlag.load())
    {
        MessageBoxW(parent, L"ISO extraction was cancelled.", L"Top Spin 4", MB_OK | MB_ICONINFORMATION);
    }
    else if (!extractError.empty())
    {
        std::wstring err(extractError.begin(), extractError.end());
        MessageBoxW(parent, err.c_str(), L"Extraction Error", MB_OK | MB_ICONERROR);
    }
    else
    {
        MessageBoxW(parent, L"Extraction completed successfully!", L"Top Spin 4", MB_OK | MB_ICONINFORMATION);
    }
}

// Patch channel modal dialog
void DoCheckPatches(HWND parent)
{
    const Ts4Settings& s = GetTs4Settings();
    std::atomic<bool> cancelFlag = false;
    std::atomic<bool> doneFlag = false;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefDlgProcW;
    wc.cbWndExtra = DLGWINDOWEXTRA;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = HBRUSH(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ts4_patch_dlg";
    RegisterClassW(&wc);

    RECT r{ 0, 0, S(420), S(180) };
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ts4_patch_dlg", L"Top Spin 4 Patches",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
        r.right - r.left, r.bottom - r.top, parent, nullptr, wc.hInstance, nullptr);

    EnableWindow(parent, FALSE);

    HFONT font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HWND lblStatus = CreateWindowW(L"STATIC", L"Checking for patches...", WS_CHILD | WS_VISIBLE, S(20), S(20), S(380), S(22), dlg, nullptr, wc.hInstance, nullptr);
    SendMessageW(lblStatus, WM_SETFONT, WPARAM(font), TRUE);

    HWND pbar = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, S(20), S(50), S(380), S(24), dlg, nullptr, wc.hInstance, nullptr);
    SendMessageW(pbar, PBM_SETRANGE32, 0, 1000);

    HWND btnClose = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, S(160), S(95), S(100), S(28), dlg, HMENU(IDCANCEL), wc.hInstance, nullptr);
    SendMessageW(btnClose, WM_SETFONT, WPARAM(font), TRUE);

    StartPatchCheckAndInstall(s.patchRepo, s.patchBranch, cancelFlag, [&](const PatchProgress& prog) {
        PostMessageW(pbar, PBM_SETPOS, WPARAM(int(prog.fraction * 1000.0f)), 0);
        SetWindowTextW(lblStatus, prog.statusText.c_str());
        if (prog.finished)
        {
            SetWindowTextW(btnClose, L"Close");
            doneFlag.store(true);
        }
    });

    MSG msg;
    bool closed = false;
    while (!closed)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_COMMAND && LOWORD(msg.wParam) == IDCANCEL)
            {
                if (!doneFlag.load())
                {
                    cancelFlag.store(true);
                    SetWindowTextW(lblStatus, L"Cancelling...");
                }
                closed = true;
                break;
            }
            if (msg.message == WM_CLOSE && msg.hwnd == dlg)
            {
                cancelFlag.store(true);
                closed = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(15);
    }

    EnableWindow(parent, TRUE);
    DestroyWindow(dlg);
    DeleteObject(font);
}

void OnImportClicked(HWND parent)
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"Select Existing Game Folder...");
    AppendMenuW(hMenu, MF_STRING, 2, L"Extract from Xbox 360 ISO Image...");

    RECT btnRect;
    GetWindowRect(g_state.importBtn, &btnRect);
    const int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, btnRect.left, btnRect.bottom, 0, parent, nullptr);
    DestroyMenu(hMenu);

    if (cmd == 1) // Folder
    {
        std::filesystem::path folder;
        if (BrowseGameFolder(parent, folder))
        {
            g_state.gameDir = folder;
            UpdateGameStatus();
        }
    }
    else if (cmd == 2) // ISO
    {
        std::filesystem::path isoPath;
        if (BrowseIsoFile(parent, isoPath))
        {
            std::filesystem::path targetDir = g_state.gameDir;
            if (targetDir.empty())
                targetDir = "game";

            int choice = MessageBoxW(parent,
                (L"Extract files to '" + targetDir.wstring() + L"'? (Click No to choose a different folder)").c_str(),
                L"Extract Target", MB_YESNOCANCEL | MB_ICONQUESTION);

            if (choice == IDNO)
            {
                if (!BrowseGameFolder(parent, targetDir))
                    return;
            }
            else if (choice == IDCANCEL)
            {
                return;
            }

            DoExtractIso(parent, isoPath, targetDir);
            g_state.gameDir = targetDir;
            UpdateGameStatus();
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case kImportBtn:
            OnImportClicked(hwnd);
            return 0;
        case kPatchBtn:
            DoCheckPatches(hwnd);
            return 0;
        case kPlayBtn:
            ApplySettingsFromUi();
            g_state.play = true;
            DestroyWindow(hwnd);
            return 0;
        case kQuitBtn:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_state.done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

bool RunLauncher(std::filesystem::path& gameDir)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    if (getenv("TS4_NO_LAUNCHER") || getenv("FM2_NO_LAUNCHER") || getenv("TS4_TOUR") || getenv("TS4_HEADLESS"))
        return true;

    Ts4Settings& s = GetTs4Settings();
    g_state.resolutions = Resolutions(s);
    g_state.gameDir = gameDir;
    g_state.play = false;
    g_state.done = false;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = HBRUSH(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ts4_launcher";
    RegisterClassW(&wc);

    RECT r{ 0, 0, S(420), S(490) };
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&r, style, FALSE);

    const int ww = r.right - r.left;
    const int wh = r.bottom - r.top;
    g_state.window = CreateWindowW(L"ts4_launcher", L"Top Spin 4 - Launcher", style,
        (GetSystemMetrics(SM_CXSCREEN) - ww) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - wh) / 2,
        ww, wh, nullptr, nullptr, wc.hInstance, nullptr);

    HFONT font = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT fontBold = CreateFontW(-S(15), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    const HWND w = g_state.window;
    const int lx = 20, cx = 150, cw = 240;
    int y = 16;

    // Profile
    Label(w, L"Profile name", lx, y, 120, 20, font);
    std::wstring name(s.profile.begin(), s.profile.end());
    g_state.profile = Control(w, L"EDIT", name.c_str(), WS_BORDER | ES_AUTOHSCROLL, cx, y, cw, 24, kProfile, font);
    SendMessageW(g_state.profile, EM_SETLIMITTEXT, 15, 0);
    y += 36;

    // Resolution
    Label(w, L"Resolution", lx, y, 120, 20, font);
    g_state.resolution = Control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, cx, y, cw, 300, kResolution, font);
    for (size_t i = 0; i < g_state.resolutions.size(); i++)
    {
        wchar_t t[32];
        swprintf(t, 32, L"%u x %u", g_state.resolutions[i].first, g_state.resolutions[i].second);
        SendMessageW(g_state.resolution, CB_ADDSTRING, 0, LPARAM(t));
        if (g_state.resolutions[i] == std::pair{ s.width, s.height })
            SendMessageW(g_state.resolution, CB_SETCURSEL, i, 0);
    }
    if (SendMessageW(g_state.resolution, CB_GETCURSEL, 0, 0) < 0)
        SendMessageW(g_state.resolution, CB_SETCURSEL, 0, 0);
    y += 36;

    // Aspect
    Label(w, L"Aspect ratio", lx, y, 120, 20, font);
    g_state.aspect = Control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST, cx, y, cw, 200, kAspect, font);
    for (size_t i = 0; i < std::size(kAspects); i++)
    {
        SendMessageW(g_state.aspect, CB_ADDSTRING, 0, LPARAM(kAspectNames[i]));
        if (kAspects[i] == std::pair{ s.aspectW, s.aspectH })
            SendMessageW(g_state.aspect, CB_SETCURSEL, i, 0);
    }
    if (SendMessageW(g_state.aspect, CB_GETCURSEL, 0, 0) < 0)
        SendMessageW(g_state.aspect, CB_SETCURSEL, 0, 0);
    y += 36;

    // Display mode
    Label(w, L"Display mode", lx, y, 120, 20, font);
    g_state.display = Control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST, cx, y, cw, 200, kDisplay, font);
    SendMessageW(g_state.display, CB_ADDSTRING, 0, LPARAM(L"Windowed"));
    SendMessageW(g_state.display, CB_ADDSTRING, 0, LPARAM(L"Fullscreen (borderless)"));
    SendMessageW(g_state.display, CB_SETCURSEL, s.fullscreen ? 1 : 0, 0);
    y += 36;

    // Anti-aliasing
    Label(w, L"Anti-aliasing", lx, y, 120, 20, font);
    g_state.msaa = Control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST, cx, y, cw, 200, kMsaa, font);
    for (size_t i = 0; i < std::size(kMsaaSamples); i++)
    {
        SendMessageW(g_state.msaa, CB_ADDSTRING, 0, LPARAM(kMsaaNames[i]));
        if (kMsaaSamples[i] == s.msaa)
            SendMessageW(g_state.msaa, CB_SETCURSEL, i, 0);
    }
    if (SendMessageW(g_state.msaa, CB_GETCURSEL, 0, 0) < 0)
        SendMessageW(g_state.msaa, CB_SETCURSEL, 0, 0);
    y += 36;

    // Upscaler
    Label(w, L"Upscaler", lx, y, 120, 20, font);
    g_state.upscaler = Control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST, cx, y, cw, 200, kUpscaler, font);
    for (size_t i = 0; i < std::size(kUpscalerNames); i++)
    {
        SendMessageW(g_state.upscaler, CB_ADDSTRING, 0, LPARAM(kUpscalerNames[i]));
    }
    SendMessageW(g_state.upscaler, CB_SETCURSEL, (s.upscaler == Ts4Settings::Upscaler::Fsr1) ? 1 : 0, 0);
    y += 38;

    // Skip intro
    g_state.skipIntro = Control(w, L"BUTTON", L"Skip intro videos", BS_AUTOCHECKBOX, lx, y, 360, 24, kSkipIntro, font);
    SendMessageW(g_state.skipIntro, BM_SETCHECK, s.skipIntro ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 30;

    // Faster geometry
    g_state.fastGeometry = Control(w, L"BUTTON", L"Faster geometry (experimental)", BS_AUTOCHECKBOX, lx, y, 360, 24, kFastGeometry, font);
    SendMessageW(g_state.fastGeometry, BM_SETCHECK, s.fastGeometry ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 34;

    // Game Files Status Label
    g_state.statusLabel = Label(w, L"Game Status: Checking...", lx, y, 370, 22, font);
    y += 32;

    // Action buttons row 1: Import & Patches
    g_state.importBtn = Control(w, L"BUTTON", L"Import Game...", BS_PUSHBUTTON, lx, y, 175, 30, kImportBtn, font);
    g_state.patchBtn = Control(w, L"BUTTON", L"Check Patches", BS_PUSHBUTTON, lx + 195, y, 175, 30, kPatchBtn, font);
    y += 42;

    // Action buttons row 2: Play & Quit
    g_state.playBtn = Control(w, L"BUTTON", L"Play", BS_DEFPUSHBUTTON, lx, y, 175, 34, kPlayBtn, fontBold);
    g_state.quitBtn = Control(w, L"BUTTON", L"Quit", BS_PUSHBUTTON, lx + 195, y, 175, 34, kQuitBtn, font);

    UpdateGameStatus();

    ShowWindow(w, SW_SHOW);
    SetForegroundWindow(w);
    SetFocus(g_state.gameFilesValid ? g_state.playBtn : g_state.importBtn);

    MSG m;
    while (!g_state.done && GetMessageW(&m, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(w, &m))
        {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    DeleteObject(font);
    DeleteObject(fontBold);

    if (!g_state.play)
        return false;

    SaveTs4Settings();
    gameDir = g_state.gameDir;
    return true;
}
