// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CacheManager.h"

#include "../util/Hash.h"
#include "../util/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <vector>

#pragma comment(lib, "shell32.lib")

namespace npps3 {
namespace {

bool IsReservedName(const std::wstring& base)
{
    static const wchar_t* kReserved[] = {
        L"CON", L"PRN", L"AUX", L"NUL",
        L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
        L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9",
    };
    for (const wchar_t* r : kReserved)
        if (::_wcsicmp(base.c_str(), r) == 0)
            return true;
    return false;
}

} // namespace

std::wstring CacheManager::SanitizeComponent(const std::wstring& name, size_t maxLen)
{
    std::wstring out;
    out.reserve(name.size());
    for (wchar_t c : name)
    {
        if (c < 32 || c == L'<' || c == L'>' || c == L':' || c == L'"' ||
            c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*')
            out.push_back(L'_');
        else
            out.push_back(c);
    }
    while (!out.empty() && (out.back() == L'.' || out.back() == L' '))
        out.pop_back();
    if (out.empty())
        out = L"_";

    size_t dot = out.find_last_of(L'.');
    std::wstring base = dot == std::wstring::npos ? out : out.substr(0, dot);
    if (IsReservedName(base))
        out = L"_" + out;

    if (out.size() > maxLen)
    {
        // Keep the extension when truncating so Notepad++ still picks the lexer.
        std::wstring ext;
        size_t d = out.find_last_of(L'.');
        if (d != std::wstring::npos && out.size() - d <= 12)
            ext = out.substr(d);
        if (ext.size() >= maxLen)
            ext.clear();
        out = out.substr(0, maxLen - ext.size()) + ext;
    }
    return out;
}

std::wstring CacheManager::LocalPathFor(const std::string& profileId,
                                        const std::string& bucket,
                                        const std::string& key) const
{
    // Hash disambiguates keys that sanitize to the same local name and keeps
    // the mapping stable across sessions.
    std::string hashInput = profileId + "\n" + bucket + "\n" + key;
    std::string hash = Sha256Hex(hashInput).substr(0, 16);

    std::string leaf = key;
    size_t slash = leaf.find_last_of('/');
    if (slash != std::string::npos)
        leaf = leaf.substr(slash + 1);
    if (leaf.empty())
        leaf = "object";

    std::wstring profileDir = SanitizeComponent(Utf8ToWide(profileId).substr(0, 8), 8);
    std::wstring bucketDir = SanitizeComponent(Utf8ToWide(bucket), 60);
    std::wstring fileName = Utf8ToWide(hash) + L"-" + SanitizeComponent(Utf8ToWide(leaf), 60);

    std::wstring path = m_root;
    if (!path.empty() && path.back() != L'\\')
        path.push_back(L'\\');
    path += profileDir;
    path.push_back(L'\\');
    path += bucketDir;
    path.push_back(L'\\');
    path += fileName;
    return path;
}

bool CacheManager::EnsureParentDirs(const std::wstring& filePath)
{
    size_t pos = filePath.find_last_of(L'\\');
    if (pos == std::wstring::npos)
        return false;
    std::wstring dir = filePath.substr(0, pos);
    int r = ::SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

int CacheManager::CleanupStale(int days,
                               const std::function<bool(const std::wstring&)>& keepPredicate) const
{
    if (m_root.empty() || days <= 0)
        return 0;

    ULARGE_INTEGER cutoff{};
    {
        FILETIME now{};
        ::GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER n{now.dwLowDateTime, now.dwHighDateTime};
        n.QuadPart -= static_cast<ULONGLONG>(days) * 24ull * 3600ull * 10'000'000ull;
        cutoff = n;
    }

    int removed = 0;
    std::vector<std::wstring> dirs{m_root};
    while (!dirs.empty())
    {
        std::wstring dir = dirs.back();
        dirs.pop_back();

        WIN32_FIND_DATAW fd{};
        HANDLE find = ::FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            if (::wcscmp(fd.cFileName, L".") == 0 || ::wcscmp(fd.cFileName, L"..") == 0)
                continue;
            std::wstring path = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                dirs.push_back(path);
                continue;
            }
            ULARGE_INTEGER wt{fd.ftLastWriteTime.dwLowDateTime, fd.ftLastWriteTime.dwHighDateTime};
            if (wt.QuadPart < cutoff.QuadPart)
            {
                if (keepPredicate && keepPredicate(path))
                    continue;
                if (::DeleteFileW(path.c_str()))
                    ++removed;
            }
        } while (::FindNextFileW(find, &fd));
        ::FindClose(find);
    }
    return removed;
}

} // namespace npps3
