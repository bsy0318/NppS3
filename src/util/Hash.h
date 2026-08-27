// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>
#include <string_view>

namespace npps3 {

using Sha256Digest = std::array<unsigned char, 32>;

// SHA-256 / HMAC-SHA256 via Windows CNG (bcrypt). Used for SigV4 signing and
// content hashing; no third-party crypto dependency.
Sha256Digest Sha256(const void* data, size_t len);
Sha256Digest Sha256(std::string_view data);

// Streams a file through SHA-256; returns false on I/O failure.
bool Sha256File(const std::wstring& path, Sha256Digest& out);

Sha256Digest HmacSha256(const void* key, size_t keyLen, std::string_view data);

std::string Sha256Hex(std::string_view data);

} // namespace npps3
