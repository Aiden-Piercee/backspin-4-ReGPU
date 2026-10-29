#include "launcher_patch.h"

#include <algorithm>
#include <bcrypt.h>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <winhttp.h>

#include "user_dir.h"
#include "log.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace
{
std::string ComputeSha256(const uint8_t* data, size_t length)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::string hex;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0)
    {
        DWORD hashObjSize = 0, dataLen = 0;
        if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dataLen, 0) >= 0)
        {
            std::vector<uint8_t> hashObj(hashObjSize);
            if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0) >= 0)
            {
                if (BCryptHashData(hHash, (PUCHAR)data, (ULONG)length, 0) >= 0)
                {
                    uint8_t hash[32];
                    if (BCryptFinishHash(hHash, hash, sizeof(hash), 0) >= 0)
                    {
                        char buf[3];
                        for (int i = 0; i < 32; ++i)
                        {
                            snprintf(buf, sizeof(buf), "%02x", hash[i]);
                            hex += buf;
                        }
                    }
                }
                BCryptDestroyHash(hHash);
            }
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return hex;
}

std::string ComputeFileSha256(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    return ComputeSha256(data.data(), data.size());
}

bool HttpGet(const std::wstring& host, const std::wstring& path, std::vector<uint8_t>& outData, std::atomic<bool>& cancelFlag)
{
    HINTERNET session = WinHttpOpen(L"TS4-Launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
    HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;

    bool success = false;
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr))
    {
        DWORD statusCode = 0, size = sizeof(statusCode);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        if (statusCode == 200)
        {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(request, &avail) && avail > 0)
            {
                if (cancelFlag.load())
                {
                    break;
                }
                size_t oldSize = outData.size();
                outData.resize(oldSize + avail);
                DWORD read = 0;
                if (!WinHttpReadData(request, outData.data() + oldSize, avail, &read))
                {
                    outData.resize(oldSize);
                    break;
                }
                outData.resize(oldSize + read);
            }
            success = !cancelFlag.load();
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return success;
}

std::wstring ToWString(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}
} // namespace

void StartPatchCheckAndInstall(
    const std::string& repo,
    const std::string& branch,
    std::atomic<bool>& cancelFlag,
    std::function<void(const PatchProgress&)> progressCallback)
{
    std::thread([repo, branch, &cancelFlag, progressCallback]() {
        PatchProgress prog;
        prog.statusText = L"Connecting to patch channel...";
        prog.fraction = 0.05f;
        progressCallback(prog);

        std::wstring host = L"raw.githubusercontent.com";
        std::wstring manifestPath = L"/" + ToWString(repo) + L"/" + ToWString(branch) + L"/patches/manifest.txt";

        std::vector<uint8_t> manifestData;
        if (!HttpGet(host, manifestPath, manifestData, cancelFlag))
        {
            prog.finished = true;
            prog.success = false;
            prog.error = "Could not fetch manifest from " + repo + " (" + branch + ").";
            prog.statusText = L"Patch check failed.";
            progressCallback(prog);
            return;
        }

        std::string manifestStr(manifestData.begin(), manifestData.end());
        std::istringstream stream(manifestStr);
        std::string line;

        struct PatchEntry
        {
            std::string path;
            std::string sha256;
        };
        std::vector<PatchEntry> entries;

        while (std::getline(stream, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;

            std::istringstream lineStream(line);
            std::string token1, token2;
            lineStream >> token1 >> token2;

            if (token1.empty() || token2.empty())
                continue;

            // Check if token1 is 64 hex chars (sha256)
            if (token1.size() == 64 && std::all_of(token1.begin(), token1.end(), [](char c) { return isxdigit((unsigned char)c); }))
            {
                entries.push_back({ token2, token1 });
            }
            else if (token2.size() == 64 && std::all_of(token2.begin(), token2.end(), [](char c) { return isxdigit((unsigned char)c); }))
            {
                entries.push_back({ token1, token2 });
            }
        }

        if (entries.empty())
        {
            prog.finished = true;
            prog.success = true;
            prog.statusText = L"No patches in channel or manifest empty.";
            prog.fraction = 1.0f;
            progressCallback(prog);
            return;
        }

        const std::filesystem::path modsDir = DocumentsUserDir() / "mods" / branch;
        std::error_code ec;
        std::filesystem::create_directories(modsDir, ec);

        int updatedCount = 0;
        int current = 0;
        const int total = int(entries.size());

        for (const auto& entry : entries)
        {
            if (cancelFlag.load())
            {
                prog.finished = true;
                prog.success = false;
                prog.error = "Patch download cancelled by user.";
                prog.statusText = L"Cancelled.";
                progressCallback(prog);
                return;
            }

            current++;
            prog.fraction = 0.1f + 0.85f * (float(current) / float(total));
            prog.statusText = L"Checking " + ToWString(entry.path) + L"...";
            progressCallback(prog);

            const std::filesystem::path localPath = modsDir / entry.path;
            bool needDownload = true;

            if (std::filesystem::exists(localPath, ec))
            {
                std::string existingSha = ComputeFileSha256(localPath);
                if (_stricmp(existingSha.c_str(), entry.sha256.c_str()) == 0)
                {
                    needDownload = false;
                }
            }

            if (needDownload)
            {
                prog.statusText = L"Downloading " + ToWString(entry.path) + L"...";
                progressCallback(prog);

                std::wstring itemPath = L"/" + ToWString(repo) + L"/" + ToWString(branch) + L"/patches/" + ToWString(entry.path);
                std::vector<uint8_t> itemData;
                if (!HttpGet(host, itemPath, itemData, cancelFlag))
                {
                    prog.finished = true;
                    prog.success = false;
                    prog.error = "Failed to download " + entry.path;
                    prog.statusText = L"Download failed.";
                    progressCallback(prog);
                    return;
                }

                std::string downloadedSha = ComputeSha256(itemData.data(), itemData.size());
                if (_stricmp(downloadedSha.c_str(), entry.sha256.c_str()) != 0)
                {
                    prog.finished = true;
                    prog.success = false;
                    prog.error = "SHA-256 verification failed for " + entry.path;
                    prog.statusText = L"Hash verification failed.";
                    progressCallback(prog);
                    return;
                }

                std::filesystem::create_directories(localPath.parent_path(), ec);
                std::ofstream out(localPath, std::ios::binary);
                out.write(reinterpret_cast<const char*>(itemData.data()), itemData.size());
                updatedCount++;
            }
        }

        prog.finished = true;
        prog.success = true;
        prog.fraction = 1.0f;
        if (updatedCount > 0)
            prog.statusText = L"Patches up to date (" + std::to_wstring(updatedCount) + L" files updated).";
        else
            prog.statusText = L"Patches are already up to date.";
        progressCallback(prog);
    }).detach();
}

