#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <windows.h>

struct ExtractProgress
{
    float fraction = 0.0f; // 0.0 to 1.0
    uint64_t bytesExtracted = 0;
    uint64_t totalBytes = 0;
    std::wstring currentFile;
};

// Validates that required Top Spin 4 game files exist in dir.
bool ValidateGameDir(const std::filesystem::path& dir, std::string* missingReport = nullptr);

// Opens native Windows folder picker.
bool BrowseGameFolder(HWND parent, std::filesystem::path& outPath);

// Opens native Windows file picker for ISO image.
bool BrowseIsoFile(HWND parent, std::filesystem::path& outPath);

// Extracts Xbox 360 XDVDFS ISO image to destDir.
bool ExtractXdvdfsIso(
    const std::filesystem::path& isoPath,
    const std::filesystem::path& destDir,
    std::atomic<bool>& cancelFlag,
    std::function<void(const ExtractProgress&)> progressCallback,
    std::string& errorOut
);

