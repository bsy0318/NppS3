// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProfileManager.h"

#include "../security/CredentialStore.h"
#include "../util/StringUtil.h"

#include <tinyxml2.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <algorithm>

#pragma comment(lib, "ole32.lib")

namespace npps3 {

const char* ProviderToString(Provider p)
{
    switch (p)
    {
    case Provider::AmazonS3: return "aws";
    case Provider::CloudflareR2: return "r2";
    case Provider::CustomS3: return "custom";
    }
    return "custom";
}

Provider ProviderFromString(const std::string& s)
{
    if (s == "aws") return Provider::AmazonS3;
    if (s == "r2") return Provider::CloudflareR2;
    return Provider::CustomS3;
}

bool ProfileManager::Load()
{
    if (m_configFile.empty())
        return false;
    HANDLE file = ::CreateFileW(m_configFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false; // first run: no config yet
    LARGE_INTEGER sz{};
    ::GetFileSizeEx(file, &sz);
    std::string xml(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ::ReadFile(file, xml.data(), static_cast<DWORD>(xml.size()), &read, nullptr);
    ::CloseHandle(file);
    if (!ok)
        return false;
    xml.resize(read);
    return DeserializeFromXml(xml);
}

bool ProfileManager::Save() const
{
    if (m_configFile.empty())
        return false;
    std::string xml = SerializeToXml();

    // Write to a temp file and swap, so a crash mid-write cannot corrupt config.
    std::wstring tmp = m_configFile + L".tmp";
    HANDLE file = ::CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    BOOL ok = ::WriteFile(file, xml.data(), static_cast<DWORD>(xml.size()), &written, nullptr);
    ::CloseHandle(file);
    if (!ok || written != xml.size())
    {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return ::MoveFileExW(tmp.c_str(), m_configFile.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

std::string ProfileManager::SerializeToXml() const
{
    tinyxml2::XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());
    tinyxml2::XMLElement* root = doc.NewElement("NppS3Config");
    root->SetAttribute("version", 1);
    doc.InsertEndChild(root);

    tinyxml2::XMLElement* settings = doc.NewElement("Settings");
    settings->SetAttribute("activeProfileId", m_settings.activeProfileId.c_str());
    settings->SetAttribute("staleCacheDays", m_settings.staleCacheDays);
    settings->SetAttribute("language", m_settings.language.c_str());
    root->InsertEndChild(settings);

    tinyxml2::XMLElement* profiles = doc.NewElement("Profiles");
    root->InsertEndChild(profiles);
    for (const Profile& p : m_profiles)
    {
        tinyxml2::XMLElement* el = doc.NewElement("Profile");
        el->SetAttribute("id", p.id.c_str());
        el->SetAttribute("name", p.name.c_str());
        el->SetAttribute("provider", ProviderToString(p.provider));
        el->SetAttribute("endpoint", p.endpoint.c_str());
        el->SetAttribute("region", p.region.c_str());
        el->SetAttribute("accessKeyId", p.accessKeyId.c_str());
        el->SetAttribute("defaultBucket", p.defaultBucket.c_str());
        el->SetAttribute("defaultPrefix", p.defaultPrefix.c_str());
        el->SetAttribute("pathStyle", p.pathStyle ? 1 : 0);
        el->SetAttribute("autoUpload", p.autoUploadOnSave ? 1 : 0);
        profiles->InsertEndChild(el);
    }

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return std::string(printer.CStr(), printer.CStrSize() > 0 ? printer.CStrSize() - 1 : 0);
}

bool ProfileManager::DeserializeFromXml(const std::string& xml)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        return false;
    const tinyxml2::XMLElement* root = doc.FirstChildElement("NppS3Config");
    if (!root)
        return false;

    m_profiles.clear();
    m_settings = PluginSettings{};

    if (const tinyxml2::XMLElement* settings = root->FirstChildElement("Settings"))
    {
        const char* active = settings->Attribute("activeProfileId");
        if (active)
            m_settings.activeProfileId = active;
        settings->QueryIntAttribute("staleCacheDays", &m_settings.staleCacheDays);
        if (m_settings.staleCacheDays < 1)
            m_settings.staleCacheDays = 14;
        const char* lang = settings->Attribute("language");
        if (lang)
            m_settings.language = lang;
    }

    const tinyxml2::XMLElement* profiles = root->FirstChildElement("Profiles");
    for (const tinyxml2::XMLElement* el = profiles ? profiles->FirstChildElement("Profile") : nullptr;
         el; el = el->NextSiblingElement("Profile"))
    {
        Profile p;
        auto attr = [&](const char* name) -> std::string {
            const char* v = el->Attribute(name);
            return v ? v : "";
        };
        p.id = attr("id");
        p.name = attr("name");
        p.provider = ProviderFromString(attr("provider"));
        p.endpoint = attr("endpoint");
        p.region = attr("region");
        p.accessKeyId = attr("accessKeyId");
        p.defaultBucket = attr("defaultBucket");
        p.defaultPrefix = attr("defaultPrefix");
        int b = 1;
        el->QueryIntAttribute("pathStyle", &b);
        p.pathStyle = b != 0;
        b = 1;
        el->QueryIntAttribute("autoUpload", &b);
        p.autoUploadOnSave = b != 0;
        if (p.region.empty())
            p.region = "auto";
        if (!p.id.empty())
            m_profiles.push_back(std::move(p));
    }
    return true;
}

const Profile* ProfileManager::FindById(const std::string& id) const
{
    auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
                           [&](const Profile& p) { return p.id == id; });
    return it != m_profiles.end() ? &*it : nullptr;
}

Profile* ProfileManager::FindById(const std::string& id)
{
    auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
                           [&](const Profile& p) { return p.id == id; });
    return it != m_profiles.end() ? &*it : nullptr;
}

void ProfileManager::AddOrUpdate(const Profile& profile)
{
    if (Profile* existing = FindById(profile.id))
        *existing = profile;
    else
        m_profiles.push_back(profile);
}

void ProfileManager::Remove(const std::string& id)
{
    CredentialStore::Remove(CredentialStore::TargetForProfile(Utf8ToWide(id)));
    m_profiles.erase(std::remove_if(m_profiles.begin(), m_profiles.end(),
                                    [&](const Profile& p) { return p.id == id; }),
                     m_profiles.end());
    if (m_settings.activeProfileId == id)
        m_settings.activeProfileId.clear();
}

bool ProfileManager::SetSecret(const std::string& profileId, const std::string& secret) const
{
    return CredentialStore::Save(CredentialStore::TargetForProfile(Utf8ToWide(profileId)), secret);
}

std::optional<std::string> ProfileManager::GetSecret(const std::string& profileId) const
{
    return CredentialStore::Load(CredentialStore::TargetForProfile(Utf8ToWide(profileId)));
}

std::string ProfileManager::GenerateId()
{
    GUID guid{};
    ::CoCreateGuid(&guid);
    char buf[40];
    ::_snprintf_s(buf, _TRUNCATE,
                  "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  guid.Data1, guid.Data2, guid.Data3,
                  guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                  guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return buf;
}

} // namespace npps3
