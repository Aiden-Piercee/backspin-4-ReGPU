// The user folder (saves, settings, caches, title update, DLC): Documents\fm2, in the Documents folder
// Windows reports, which the launcher and the installer use too. A moved Documents folder (E:\Documents)
// isn't %USERPROFILE%\Documents: building the path from that made the game miss the title update and
// ignore the launcher's settings (GitHub issue #2). If only the old %USERPROFILE%\Documents\fm2 exists
// (the workaround for that bug), it's kept.
#include "user_dir.h"

#include <windows.h>

#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

const std::filesystem::path& DocumentsUserDir()
{
    static const std::filesystem::path dir = [] {
        // TS4_USER_DIR=<folder>: another user folder (side-by-side test instances, each with its
        // own saves and shader caches).
        if (const wchar_t* own = _wgetenv(L"TS4_USER_DIR"); own && *own)
            return std::filesystem::absolute(own);
        std::error_code ec;
        std::filesystem::path documents;
        PWSTR known = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &known)) && known)
            documents = known;
        CoTaskMemFree(known);
        std::filesystem::path old;
        if (wchar_t profile[MAX_PATH]; GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH))
            old = std::filesystem::path(profile) / "Documents" / "fm2";
        if (documents.empty())
            return old.empty() ? std::filesystem::path("user") : old;
        const std::filesystem::path current = documents / "fm2";
        if (!old.empty() && !std::filesystem::exists(current, ec) && std::filesystem::exists(old, ec) && !std::filesystem::equivalent(old.parent_path(), documents, ec))
            return old;
        return current;
    }();
    return dir;
}
