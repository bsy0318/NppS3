// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CredentialStore.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

#pragma comment(lib, "advapi32.lib")

namespace npps3 {

bool CredentialStore::Save(const std::wstring& targetName, const std::string& secret)
{
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t*>(targetName.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
    cred.CredentialBlob = reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE; // survives logoff, per-user encrypted
    cred.UserName = const_cast<wchar_t*>(L"NppS3");
    return ::CredWriteW(&cred, 0) != 0;
}

std::optional<std::string> CredentialStore::Load(const std::wstring& targetName)
{
    PCREDENTIALW cred = nullptr;
    if (!::CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &cred))
        return std::nullopt;
    std::string secret(reinterpret_cast<char*>(cred->CredentialBlob), cred->CredentialBlobSize);
    // Scrub the OS copy before releasing it.
    ::SecureZeroMemory(cred->CredentialBlob, cred->CredentialBlobSize);
    ::CredFree(cred);
    return secret;
}

bool CredentialStore::Remove(const std::wstring& targetName)
{
    return ::CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0) != 0;
}

std::wstring CredentialStore::TargetForProfile(const std::wstring& profileId)
{
    return L"NppS3/" + profileId;
}

} // namespace npps3
