// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>

namespace npps3 {

// Secret Access Keys live in the Windows Credential Manager, encrypted
// per-user by the OS. Profile configuration stores only the target name
// reference, never the secret itself.
class CredentialStore
{
public:
    // targetName example: "NppS3/<profile-id>"
    static bool Save(const std::wstring& targetName, const std::string& secret);
    static std::optional<std::string> Load(const std::wstring& targetName);
    static bool Remove(const std::wstring& targetName);

    static std::wstring TargetForProfile(const std::wstring& profileId);
};

} // namespace npps3
