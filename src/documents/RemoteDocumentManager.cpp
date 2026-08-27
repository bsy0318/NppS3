// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RemoteDocumentManager.h"

#include "../util/StringUtil.h"

#include <tinyxml2.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace npps3 {

std::wstring RemoteDocumentManager::NormalizePath(const std::wstring& path)
{
    wchar_t full[MAX_PATH * 2];
    DWORD n = ::GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
    std::wstring out = (n > 0 && n < std::size(full)) ? std::wstring(full, n) : path;
    for (wchar_t& c : out)
        c = static_cast<wchar_t>(::towlower(c));
    return out;
}

bool RemoteDocumentManager::Load()
{
    if (m_storeFile.empty())
        return false;
    HANDLE file = ::CreateFileW(m_storeFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
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

bool RemoteDocumentManager::Save() const
{
    if (m_storeFile.empty())
        return false;
    std::string xml = SerializeToXml();
    std::wstring tmp = m_storeFile + L".tmp";
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
    return ::MoveFileExW(tmp.c_str(), m_storeFile.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

std::string RemoteDocumentManager::SerializeToXml() const
{
    tinyxml2::XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());
    tinyxml2::XMLElement* root = doc.NewElement("NppS3Documents");
    root->SetAttribute("version", 1);
    doc.InsertEndChild(root);

    for (const auto& [normPath, d] : m_docs)
    {
        tinyxml2::XMLElement* el = doc.NewElement("Document");
        el->SetAttribute("profileId", d.profileId.c_str());
        el->SetAttribute("bucket", d.bucket.c_str());
        el->SetAttribute("key", d.key.c_str());
        el->SetAttribute("localPath", WideToUtf8(d.localPath).c_str());
        el->SetAttribute("etag", d.etag.c_str());
        el->SetAttribute("lastModified", d.lastModified.c_str());
        el->SetAttribute("autoUpload", d.autoUpload ? 1 : 0);
        root->InsertEndChild(el);
    }

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return std::string(printer.CStr(), printer.CStrSize() > 0 ? printer.CStrSize() - 1 : 0);
}

bool RemoteDocumentManager::DeserializeFromXml(const std::string& xml)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        return false;
    const tinyxml2::XMLElement* root = doc.FirstChildElement("NppS3Documents");
    if (!root)
        return false;

    m_docs.clear();
    for (const tinyxml2::XMLElement* el = root->FirstChildElement("Document");
         el; el = el->NextSiblingElement("Document"))
    {
        auto attr = [&](const char* name) -> std::string {
            const char* v = el->Attribute(name);
            return v ? v : "";
        };
        RemoteDocument d;
        d.profileId = attr("profileId");
        d.bucket = attr("bucket");
        d.key = attr("key");
        d.localPath = Utf8ToWide(attr("localPath"));
        d.etag = attr("etag");
        d.lastModified = attr("lastModified");
        int b = 1;
        el->QueryIntAttribute("autoUpload", &b);
        d.autoUpload = b != 0;
        if (!d.localPath.empty() && !d.key.empty())
            m_docs[NormalizePath(d.localPath)] = std::move(d);
    }
    return true;
}

void RemoteDocumentManager::Register(const RemoteDocument& doc)
{
    m_docs[NormalizePath(doc.localPath)] = doc;
}

const RemoteDocument* RemoteDocumentManager::FindByLocalPath(const std::wstring& localPath) const
{
    auto it = m_docs.find(NormalizePath(localPath));
    return it != m_docs.end() ? &it->second : nullptr;
}

RemoteDocument* RemoteDocumentManager::FindByLocalPath(const std::wstring& localPath)
{
    auto it = m_docs.find(NormalizePath(localPath));
    return it != m_docs.end() ? &it->second : nullptr;
}

void RemoteDocumentManager::UpdateRemoteState(const std::wstring& localPath,
                                              const std::string& etag,
                                              const std::string& lastModified)
{
    if (RemoteDocument* d = FindByLocalPath(localPath))
    {
        d->etag = etag;
        d->lastModified = lastModified;
    }
}

void RemoteDocumentManager::Remove(const std::wstring& localPath)
{
    m_docs.erase(NormalizePath(localPath));
}

std::vector<RemoteDocument> RemoteDocumentManager::All() const
{
    std::vector<RemoteDocument> out;
    out.reserve(m_docs.size());
    for (const auto& [k, v] : m_docs)
        out.push_back(v);
    return out;
}

} // namespace npps3
