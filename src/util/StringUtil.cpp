// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace npps3 {

std::wstring Utf8ToWide(std::string_view utf8)
{
    if (utf8.empty())
        return {};
    int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0)
        return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

std::string WideToUtf8(std::wstring_view wide)
{
    if (wide.empty())
        return {};
    int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};
    std::string out(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string ToLowerAscii(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string Trim(std::string_view s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
        ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
        --e;
    return std::string(s.substr(b, e - b));
}

std::string UriEncode(std::string_view value, bool keepSlash)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            out.push_back(static_cast<char>(c));
        }
        else if (c == '/' && keepSlash)
        {
            out.push_back('/');
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::string HexLower(const unsigned char* data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0x0F]);
    }
    return out;
}

std::vector<std::string> SplitKey(std::string_view key, char sep)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= key.size())
    {
        size_t pos = key.find(sep, start);
        if (pos == std::string_view::npos)
        {
            if (start < key.size())
                parts.emplace_back(key.substr(start));
            break;
        }
        if (pos > start)
            parts.emplace_back(key.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

bool StartsWith(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b)
{
    if (a.size() != b.size())
        return false;
    return ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
                                  b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::string MaskSensitive(std::string_view value)
{
    if (value.size() <= 4)
        return "****";
    std::string out(value.substr(0, 2));
    out += "****";
    out += value.substr(value.size() - 2);
    return out;
}

} // namespace npps3
