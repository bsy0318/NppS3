// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CacheManager.h"

#include "../util/Hash.h"
#include "../util/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>

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
    return EnsureDirectoryTree(filePath.substr(0, pos));
}

bool CacheManager::EnsureDirectoryTree(const std::wstring& dir)
{
    if (dir.empty())
        return false;

    // Start past the root ("C:\", "\\?\C:\", "\\server\share\") so the loop
    // never tries to create a drive or a UNC share.
    size_t pos = 0;
    if (dir.compare(0, 2, L"\\\\") == 0)
    {
        size_t seps = 0;
        while (pos < dir.size() && seps < 4)
        {
            if (dir[pos] == L'\\')
                ++seps;
            ++pos;
        }
    }
    else
    {
        size_t colon = dir.find(L':');
        pos = colon == std::wstring::npos ? 0 : colon + 1;
    }

    for (;;)
    {
        size_t sep = dir.find(L'\\', pos + 1);
        std::wstring level = sep == std::wstring::npos ? dir : dir.substr(0, sep);
        if (!level.empty() && level.back() != L':')
        {
            if (!::CreateDirectoryW(level.c_str(), nullptr) &&
                ::GetLastError() != ERROR_ALREADY_EXISTS)
                return false;
        }
        if (sep == std::wstring::npos)
            break;
        pos = sep;
    }
    return true;
}

std::wstring CacheManager::RelativePathForKey(const std::string& key)
{
    // SplitKey drops empty segments, so "a//b/" yields "a\b" and a trailing
    // slash (a folder marker) yields the directory path with no file name.
    std::wstring out;
    for (const std::string& segment : SplitKey(key, '/'))
    {
        if (!out.empty())
            out.push_back(L'\\');
        out += SanitizeComponent(Utf8ToWide(segment), 100);
    }
    return out;
}

std::wstring CacheManager::ExtendedPath(const std::wstring& absolutePath)
{
    if (absolutePath.size() < MAX_PATH || absolutePath.compare(0, 4, L"\\\\?\\") == 0)
        return absolutePath;
    if (absolutePath.compare(0, 2, L"\\\\") == 0)
        return L"\\\\?\\UNC" + absolutePath.substr(1);
    return L"\\\\?\\" + absolutePath;
}

int CacheManager::CleanupStale(int days,
                               const std::function<bool(const std::wstring&)>& keepPredicate) const
{
    if (days <= 0)
        return 0;

    FILETIME now{};
    ::GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER cutoff{now.dwLowDateTime, now.dwHighDateTime};
    cutoff.QuadPart -= static_cast<ULONGLONG>(days) * 24ull * 3600ull * 10'000'000ull;
    return Sweep(cutoff.QuadPart, keepPredicate);
}

int CacheManager::RemoveAll(const std::function<bool(const std::wstring&)>& keepPredicate) const
{
    return Sweep(MAXULONGLONG, keepPredicate);
}

int CacheManager::Sweep(unsigned long long cutoffFileTime,
                        const std::function<bool(const std::wstring&)>& keepPredicate) const
{
    if (m_root.empty())
        return 0;

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
            if (wt.QuadPart < cutoffFileTime)
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
