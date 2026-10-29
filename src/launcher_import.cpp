#include "launcher_import.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <queue>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#pragma comment(lib, "comdlg32.lib")
#include <unordered_set>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
#pragma pack(push, 1)
struct XdvdfsVolumeDescriptor
{
    char magic[20]; // "MICROSOFT*XBOX*MEDIA"
    uint32_t rootSector;
    uint32_t rootSize;
    uint64_t creationTime;
};

struct XdvdfsRawEntry
{
    uint16_t leftSubTree;   // byte offset = leftSubTree * 4
    uint16_t rightSubTree;  // byte offset = rightSubTree * 4
    uint32_t sector;
    uint32_t size;
    uint8_t attributes;     // 0x10 = directory
    uint8_t nameLength;
};
#pragma pack(pop)

const char kXdvdfsMagic[20] = { 'M','I','C','R','O','S','O','F','T','*','X','B','O','X','*','M','E','D','I','A' };

struct FileItem
{
    std::filesystem::path relPath;
    uint32_t sector;
    uint32_t size;
};
} // namespace

bool ValidateGameDir(const std::filesystem::path& dir, std::string* missingReport)
{
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
    {
        if (missingReport)
            *missingReport = "Game folder does not exist: " + dir.string();
        return false;
    }

    const std::filesystem::path defaultXex = dir / "default.xex";
    const std::filesystem::path loaderXex = dir / "Loader_DLL.xex";
    const std::filesystem::path swingXex = dir / "Swing_DLL.xex";

    std::vector<std::string> missing;
    if (!std::filesystem::exists(defaultXex, ec)) missing.push_back("default.xex");
    if (!std::filesystem::exists(loaderXex, ec)) missing.push_back("Loader_DLL.xex");
    if (!std::filesystem::exists(swingXex, ec)) missing.push_back("Swing_DLL.xex");

    if (!missing.empty())
    {
        if (missingReport)
        {
            std::string msg = "Missing required files in " + dir.filename().string() + ": ";
            for (size_t i = 0; i < missing.size(); ++i)
            {
                if (i > 0) msg += ", ";
                msg += missing[i];
            }
            *missingReport = msg;
        }
        return false;
    }

    if (missingReport)
        *missingReport = "Valid Top Spin 4 game directory.";
    return true;
}

bool BrowseGameFolder(HWND parent, std::filesystem::path& outPath)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool coInit = SUCCEEDED(hr);

    IFileDialog* pfd = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr))
    {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions)))
            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

        pfd->SetTitle(L"Select Top Spin 4 Game Folder");

        if (SUCCEEDED(pfd->Show(parent)))
        {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(pfd->GetResult(&psi)))
            {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
                {
                    outPath = pszPath;
                    CoTaskMemFree(pszPath);
                    psi->Release();
                    pfd->Release();
                    if (coInit) CoUninitialize();
                    return true;
                }
                psi->Release();
            }
        }
        pfd->Release();
    }

    if (coInit) CoUninitialize();
    return false;
}

bool BrowseIsoFile(HWND parent, std::filesystem::path& outPath)
{
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = L"Xbox 360 Disc Image (*.iso)\0*.iso\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select Top Spin 4 ISO Image";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn))
    {
        outPath = fileName;
        return true;
    }
    return false;
}

bool ExtractXdvdfsIso(
    const std::filesystem::path& isoPath,
    const std::filesystem::path& destDir,
    std::atomic<bool>& cancelFlag,
    std::function<void(const ExtractProgress&)> progressCallback,
    std::string& errorOut)
{
    FILE* fp = nullptr;
    _wfopen_s(&fp, isoPath.c_str(), L"rb");
    if (!fp)
    {
        errorOut = "Failed to open ISO file: " + isoPath.string();
        return false;
    }

    // Find volume descriptor
    uint64_t volDescSector = 0;
    uint64_t partitionBaseSector = 0;
    bool found = false;

    // Check candidate sectors
    const uint64_t candidateBases[] = {
        0,
        32,
        0xFD90, // XGD2: 64912
        0xFD90 + 32,
        0x2080, // XGD3: 8320
        0x2080 + 32,
        0x10000,
        0x10000 + 32
    };

    XdvdfsVolumeDescriptor volDesc = {};
    for (uint64_t cand : candidateBases)
    {
        if (_fseeki64(fp, cand * 2048ULL, SEEK_SET) == 0)
        {
            if (fread(&volDesc, 1, sizeof(volDesc), fp) == sizeof(volDesc))
            {
                if (memcmp(volDesc.magic, kXdvdfsMagic, 20) == 0)
                {
                    volDescSector = cand;
                    found = true;
                    break;
                }
            }
        }
    }

    // If not found in candidates, scan first 600 MB in 2 MB blocks
    if (!found)
    {
        const size_t bufSize = 2 * 1024 * 1024;
        std::vector<uint8_t> buffer(bufSize);
        _fseeki64(fp, 0, SEEK_SET);
        for (uint64_t offset = 0; offset < 600ULL * 1024ULL * 1024ULL; offset += bufSize)
        {
            if (cancelFlag.load())
            {
                fclose(fp);
                errorOut = "Extraction cancelled by user.";
                return false;
            }
            size_t bytesRead = fread(buffer.data(), 1, bufSize, fp);
            if (bytesRead < sizeof(volDesc))
                break;

            for (size_t i = 0; i + sizeof(volDesc) <= bytesRead; i += 2048)
            {
                if (memcmp(buffer.data() + i, kXdvdfsMagic, 20) == 0)
                {
                    volDescSector = (offset + i) / 2048ULL;
                    memcpy(&volDesc, buffer.data() + i, sizeof(volDesc));
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
    }

    if (!found)
    {
        fclose(fp);
        errorOut = "Not a recognized Xbox 360 ISO image (XDVDFS volume descriptor not found).";
        return false;
    }

    // Determine partition base sector:
    // In standard XDVDFS, the volume descriptor is at relative sector 32 of the partition.
    if (volDescSector >= 32)
        partitionBaseSector = volDescSector - 32;
    else
        partitionBaseSector = volDescSector;

    // Collect all files
    std::vector<FileItem> allFiles;
    struct DirTask
    {
        uint32_t sector;
        uint32_t size;
        std::filesystem::path relDir;
    };
    std::queue<DirTask> dirQueue;
    dirQueue.push({ volDesc.rootSector, volDesc.rootSize, "" });

    while (!dirQueue.empty())
    {
        if (cancelFlag.load())
        {
            fclose(fp);
            errorOut = "Extraction cancelled by user.";
            return false;
        }

        DirTask task = dirQueue.front();
        dirQueue.pop();

        if (task.size == 0)
            continue;

        std::vector<uint8_t> table(task.size);
        uint64_t tableOffset = (partitionBaseSector + task.sector) * 2048ULL;
        if (_fseeki64(fp, tableOffset, SEEK_SET) != 0 ||
            fread(table.data(), 1, task.size, fp) != task.size)
        {
            continue;
        }

        std::unordered_set<uint32_t> visitedOffsets;
        std::queue<uint32_t> entryOffsets;
        entryOffsets.push(0);

        while (!entryOffsets.empty())
        {
            uint32_t off = entryOffsets.front();
            entryOffsets.pop();

            if (off + sizeof(XdvdfsRawEntry) > task.size)
                continue;
            if (!visitedOffsets.insert(off).second)
                continue;

            const XdvdfsRawEntry* e = reinterpret_cast<const XdvdfsRawEntry*>(table.data() + off);
            if (e->leftSubTree)
                entryOffsets.push(uint32_t(e->leftSubTree) * 4);
            if (e->rightSubTree)
                entryOffsets.push(uint32_t(e->rightSubTree) * 4);

            if (e->nameLength == 0 || off + sizeof(XdvdfsRawEntry) + e->nameLength > task.size)
                continue;

            std::string name(reinterpret_cast<const char*>(table.data() + off + sizeof(XdvdfsRawEntry)), e->nameLength);
            std::filesystem::path currentRel = task.relDir.empty() ? std::filesystem::path(name) : (task.relDir / name);

            if (e->attributes & 0x10) // Directory
            {
                dirQueue.push({ e->sector, e->size, currentRel });
            }
            else // File
            {
                allFiles.push_back({ currentRel, e->sector, e->size });
            }
        }
    }

    if (allFiles.empty())
    {
        fclose(fp);
        errorOut = "No files found inside the Xbox 360 ISO image.";
        return false;
    }

    uint64_t totalBytes = 0;
    for (const auto& fi : allFiles)
        totalBytes += fi.size;

    uint64_t bytesExtracted = 0;
    const size_t chunkSize = 1024 * 1024; // 1 MB buffer
    std::vector<uint8_t> chunk(chunkSize);

    for (const auto& fi : allFiles)
    {
        if (cancelFlag.load())
        {
            fclose(fp);
            errorOut = "Extraction cancelled by user.";
            return false;
        }

        std::filesystem::path outFilePath = destDir / fi.relPath;
        std::error_code ec;
        std::filesystem::create_directories(outFilePath.parent_path(), ec);

        FILE* outFile = nullptr;
        _wfopen_s(&outFile, outFilePath.c_str(), L"wb");
        if (!outFile)
        {
            bytesExtracted += fi.size;
            continue;
        }

        uint64_t srcOffset = (partitionBaseSector + fi.sector) * 2048ULL;
        _fseeki64(fp, srcOffset, SEEK_SET);

        uint32_t remaining = fi.size;
        while (remaining > 0)
        {
            if (cancelFlag.load())
            {
                fclose(outFile);
                fclose(fp);
                errorOut = "Extraction cancelled by user.";
                return false;
            }

            size_t toRead = std::min<size_t>(remaining, chunkSize);
            size_t nRead = fread(chunk.data(), 1, toRead, fp);
            if (nRead == 0)
                break;

            fwrite(chunk.data(), 1, nRead, outFile);
            remaining -= uint32_t(nRead);
            bytesExtracted += nRead;

            if (progressCallback)
            {
                ExtractProgress prog;
                prog.totalBytes = totalBytes;
                prog.bytesExtracted = bytesExtracted;
                prog.fraction = totalBytes > 0 ? float(double(bytesExtracted) / double(totalBytes)) : 0.0f;
                prog.currentFile = fi.relPath.wstring();
                progressCallback(prog);
            }
        }
        fclose(outFile);
    }

    fclose(fp);
    return true;
}

