// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Profile.h"

#include <optional>
#include <string>
#include <vector>

namespace npps3 {

struct PluginSettings
{
    std::string activeProfileId;
    int staleCacheDays = 14;    // cached downloads older than this are purged at startup
    std::string language = "auto"; // UI language: auto|en|ko|ja|zh|ru
};

// Persists profiles + settings as XML under the Notepad++ plugin config dir.
// Secrets are delegated to CredentialStore and never touch the XML.
class ProfileManager
{
public:
    void SetConfigFile(const std::wstring& path) { m_configFile = path; }

    bool Load();
    bool Save() const;

    // Serialization split out so unit tests can run without the filesystem.
    std::string SerializeToXml() const;
    bool DeserializeFromXml(const std::string& xml);

    const std::vector<Profile>& Profiles() const { return m_profiles; }
    const Profile* FindById(const std::string& id) const;
    Profile* FindById(const std::string& id);

    void AddOrUpdate(const Profile& profile);
    // Removes the profile and its stored credential.
    void Remove(const std::string& id);

    // Secret management (Credential Manager).
    bool SetSecret(const std::string& profileId, const std::string& secret) const;
    std::optional<std::string> GetSecret(const std::string& profileId) const;

    PluginSettings& Settings() { return m_settings; }
    const PluginSettings& Settings() const { return m_settings; }

    static std::string GenerateId();

private:
    std::wstring m_configFile;
    std::vector<Profile> m_profiles;
    PluginSettings m_settings;
};

} // namespace npps3
