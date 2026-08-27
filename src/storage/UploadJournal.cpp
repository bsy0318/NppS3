// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UploadJournal.h"

#include "../util/Hash.h"
#include "../util/StringUtil.h"

#include <tinyxml2.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>

namespace npps3 {

std::string IUploadJournal::ScopeFor(const std::string& endpoint, const std::string& accessKeyId)
{
    // A digest, not the identifiers themselves: the journal file is plain XML.
    return Sha256Hex(endpoint + "\n" + accessKeyId).substr(0, 16);
}

void FileUploadJournal::SetFile(const std::wstring& path)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_file = path;
        m_entries.clear();
    }

    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return; // no journal yet
    LARGE_INTEGER sz{};
    ::GetFileSizeEx(file, &sz);
    std::string xml(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ::ReadFile(file, xml.data(), static_cast<DWORD>(xml.size()), &read, nullptr);
    ::CloseHandle(file);
    if (!ok)
        return;
    xml.resize(read);
    DeserializeFromXml(xml);
}

std::string FileUploadJournal::SerializeToXml() const
{
    tinyxml2::XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());
    tinyxml2::XMLElement* root = doc.NewElement("NppS3Uploads");
    doc.InsertEndChild(root);

    for (const UploadJournalEntry& entry : m_entries)
    {
        tinyxml2::XMLElement* e = doc.NewElement("Upload");
        e->SetAttribute("scope", entry.scope.c_str());
        e->SetAttribute("bucket", entry.bucket.c_str());
        e->SetAttribute("key", entry.key.c_str());
        e->SetAttribute("uploadId", entry.uploadId.c_str());
        e->SetAttribute("localPath", WideToUtf8(entry.localPath).c_str());
        e->SetAttribute("fileSize", std::to_string(entry.fileSize).c_str());
        e->SetAttribute("fileTime", std::to_string(entry.fileTime).c_str());
        e->SetAttribute("partSize", std::to_string(entry.partSize).c_str());
        for (const MultipartPart& part : entry.parts)
        {
            tinyxml2::XMLElement* p = doc.NewElement("Part");
            p->SetAttribute("n", part.partNumber);
            p->SetAttribute("size", std::to_string(part.size).c_str());
            p->SetAttribute("etag", part.etag.c_str());
            e->InsertEndChild(p);
        }
        root->InsertEndChild(e);
    }

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return std::string(printer.CStr(), printer.CStrSize() > 0 ? printer.CStrSize() - 1 : 0);
}

bool FileUploadJournal::DeserializeFromXml(const std::string& xml)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        return false;
    const tinyxml2::XMLElement* root = doc.FirstChildElement("NppS3Uploads");
    if (!root)
        return false;

    std::vector<UploadJournalEntry> parsed;
    for (const tinyxml2::XMLElement* e = root->FirstChildElement("Upload");
         e; e = e->NextSiblingElement("Upload"))
    {
        UploadJournalEntry entry;
        if (const char* v = e->Attribute("scope")) entry.scope = v;
        if (const char* v = e->Attribute("bucket")) entry.bucket = v;
        if (const char* v = e->Attribute("key")) entry.key = v;
        if (const char* v = e->Attribute("uploadId")) entry.uploadId = v;
        if (const char* v = e->Attribute("localPath")) entry.localPath = Utf8ToWide(v);
        if (const char* v = e->Attribute("fileSize")) entry.fileSize = ::_strtoui64(v, nullptr, 10);
        if (const char* v = e->Attribute("fileTime")) entry.fileTime = ::_strtoui64(v, nullptr, 10);
        if (const char* v = e->Attribute("partSize")) entry.partSize = ::_strtoui64(v, nullptr, 10);
        for (const tinyxml2::XMLElement* p = e->FirstChildElement("Part");
             p; p = p->NextSiblingElement("Part"))
        {
            MultipartPart part;
            part.partNumber = p->IntAttribute("n");
            if (const char* v = p->Attribute("size")) part.size = ::_strtoui64(v, nullptr, 10);
            if (const char* v = p->Attribute("etag")) part.etag = v;
            if (part.partNumber > 0)
                entry.parts.push_back(std::move(part));
        }
        if (!entry.bucket.empty() && !entry.key.empty() && !entry.uploadId.empty())
            parsed.push_back(std::move(entry));
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries = std::move(parsed);
    return true;
}

void FileUploadJournal::SaveLocked() const
{
    if (m_file.empty())
        return;
    std::string xml = SerializeToXml();

    std::wstring tmp = m_file + L".tmp";
    HANDLE file = ::CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    BOOL ok = ::WriteFile(file, xml.data(), static_cast<DWORD>(xml.size()), &written, nullptr);
    ::CloseHandle(file);
    if (!ok || written != xml.size())
    {
        ::DeleteFileW(tmp.c_str());
        return;
    }
    ::MoveFileExW(tmp.c_str(), m_file.c_str(), MOVEFILE_REPLACE_EXISTING);
}

UploadJournalEntry* FileUploadJournal::FindLocked(const std::string& scope,
                                                  const std::string& bucket,
                                                  const std::string& key)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [&](const UploadJournalEntry& e) {
                               return e.scope == scope && e.bucket == bucket && e.key == key;
                           });
    return it == m_entries.end() ? nullptr : &*it;
}

std::optional<UploadJournalEntry> FileUploadJournal::Find(const std::string& scope,
                                                          const std::string& bucket,
                                                          const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const UploadJournalEntry& e : m_entries)
        if (e.scope == scope && e.bucket == bucket && e.key == key)
            return e;
    return std::nullopt;
}

void FileUploadJournal::Begin(const UploadJournalEntry& entry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (UploadJournalEntry* existing = FindLocked(entry.scope, entry.bucket, entry.key))
        *existing = entry;
    else
        m_entries.push_back(entry);
    SaveLocked();
}

void FileUploadJournal::RecordPart(const std::string& scope, const std::string& bucket,
                                   const std::string& key, const MultipartPart& part)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    UploadJournalEntry* entry = FindLocked(scope, bucket, key);
    if (!entry)
        return;
    auto it = std::find_if(entry->parts.begin(), entry->parts.end(),
                           [&](const MultipartPart& p) { return p.partNumber == part.partNumber; });
    if (it != entry->parts.end())
        *it = part;
    else
        entry->parts.push_back(part);
    SaveLocked();
}

void FileUploadJournal::Remove(const std::string& scope, const std::string& bucket,
                               const std::string& key)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t before = m_entries.size();
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [&](const UploadJournalEntry& e) {
                                       return e.scope == scope && e.bucket == bucket &&
                                              e.key == key;
                                   }),
                    m_entries.end());
    if (m_entries.size() != before)
        SaveLocked();
}

std::vector<UploadJournalEntry> FileUploadJournal::Entries() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

} // namespace npps3
