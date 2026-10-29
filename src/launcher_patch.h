#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <windows.h>

struct PatchProgress
{
    std::wstring statusText;
    float fraction = 0.0f;
    bool finished = false;
    bool success = false;
    std::string error;
};

// Asynchronously checks and applies patches from the specified GitHub repo and branch.
void StartPatchCheckAndInstall(
    const std::string& repo,
    const std::string& branch,
    std::atomic<bool>& cancelFlag,
    std::function<void(const PatchProgress&)> progressCallback
);

