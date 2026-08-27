// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../storage/StorageError.h"

#include <string>

namespace npps3 {

// Localized error text for anything the user sees. Describe() stays English
// for logs and the CLI tools. S3 code and HTTP status are appended verbatim.
std::wstring DescribeErrorLocalized(const StorageError& e);

} // namespace npps3
