// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace npps3 {

// Returns the MIME type for an object key or file name based on its extension.
// Falls back to "application/octet-stream" for unknown extensions.
std::string MimeTypeForKey(std::string_view keyOrName);

} // namespace npps3
