// The user folder (saves, settings, caches, title update, DLC): "user" next to the executable, so a
// build is self-contained and nothing has to be hunted for in Documents.
#include "user_dir.h"

#include <windows.h>

const std::filesystem::path& DocumentsUserDir()
{
    static const std::filesystem::path dir = [] {
        // TS4_USER_DIR=<folder>: another user folder (side-by-side test instances, each with its
        // own saves and shader caches).
        if (const wchar_t* own = _wgetenv(L"TS4_USER_DIR"); own && *own)
            return std::filesystem::absolute(own);
        wchar_t exe[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (n == 0 || n == MAX_PATH)
            return std::filesystem::absolute("user");
        return std::filesystem::path(exe).parent_path() / "user";
    }();
    return dir;
}
