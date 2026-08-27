// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace npps3 {

enum class Provider
{
    AmazonS3,
    CloudflareR2,
    CustomS3,
};

const char* ProviderToString(Provider p);
Provider ProviderFromString(const std::string& s);

// A connection profile. The Secret Access Key is NOT part of this structure;
// it lives in the Windows Credential Manager keyed by the profile id.
struct Profile
{
    std::string id;             // stable GUID string
    std::string name;
    Provider provider = Provider::CloudflareR2;
    std::string endpoint;       // full URL, e.g. https://<account>.r2.cloudflarestorage.com
    std::string region = "auto";
    std::string accessKeyId;
    std::string defaultBucket;
    std::string defaultPrefix;
    bool pathStyle = true;
    bool autoUploadOnSave = true;

    bool Valid() const
    {
        return !id.empty() && !name.empty() && !endpoint.empty() && !accessKeyId.empty();
    }
};

} // namespace npps3
