// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace npps3 {

// Modal profile management dialog (list + editor + connection test).
void ShowProfilesDialog(HWND parent, HINSTANCE hInstance);

struct UploadDialogResult
{
    std::string profileId;
    std::string bucket;
    std::string key;
    std::string contentType;
};

// Upload-current-file dialog; fileName seeds the object key suggestion.
bool ShowUploadDialog(HWND parent, HINSTANCE hInstance,
                      const std::wstring& fileName, UploadDialogResult& out);

// Generic one-line text prompt. Returns false on cancel/empty.
bool ShowInputDialog(HWND parent, HINSTANCE hInstance,
                     const wchar_t* title, const wchar_t* label, std::wstring& value);

enum class ConflictChoice { Overwrite, DownloadRemote, Cancel };

// Save-time conflict prompt. remoteGone: the object vanished remotely.
ConflictChoice ShowConflictDialog(HWND parent, const std::wstring& key, bool remoteGone);

} // namespace npps3
