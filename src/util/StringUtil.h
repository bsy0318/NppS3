// NppS3 — Notepad++ plugin for S3-compatible object storage
// Copyright (C) 2026 NppS3 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace npps3 {

// UTF-8 (S3 keys, HTTP) <-> UTF-16 (Win32/Notepad++) conversion boundary.
std::wstring Utf8ToWide(std::string_view utf8);
std::string WideToUtf8(std::wstring_view wide);

std::string ToLowerAscii(std::string_view s);
std::string Trim(std::string_view s);

// RFC 3986 encoding as required by SigV4 canonical requests.
// keepSlash=true is used for object-key paths where '/' separates segments.
std::string UriEncode(std::string_view value, bool keepSlash);

std::string HexLower(const unsigned char* data, size_t len);

// Splits "a/b/c" style keys; empty segments are preserved only internally, never returned.
std::vector<std::string> SplitKey(std::string_view key, char sep);

bool StartsWith(std::string_view s, std::string_view prefix);
bool EndsWith(std::string_view s, std::string_view suffix);
bool EqualsNoCase(std::wstring_view a, std::wstring_view b);

// Masks a potentially sensitive identifier for diagnostics: keeps first/last 2 chars.
std::string MaskSensitive(std::string_view value);

} // namespace npps3
