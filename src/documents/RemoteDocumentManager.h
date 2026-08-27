// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace npps3 {

// A remote object opened for editing through NppS3.
struct RemoteDocument
{
    std::string profileId;
    std::string bucket;
    std::string key;
    std::wstring localPath;   // managed cache file opened in Notepad++
    std::string etag;         // as of last download/upload; not necessarily an MD5
    std::string lastModified; // remote timestamp at last sync
    bool autoUpload = true;
};

// Tracks which local cache files represent remote objects, so a Notepad++
// save notification can be mapped back to the originating object.
// Ordinary local files never appear here and therefore never trigger uploads.
//
// Threading: must only be used from the Notepad++ UI thread (notifications
// and marshalled transfer completions), or single-threaded in tests.
class RemoteDocumentManager
{
public:
    void SetStoreFile(const std::wstring& path) { m_storeFile = path; }

    bool Load();
    bool Save() const;

    std::string SerializeToXml() const;
    bool DeserializeFromXml(const std::string& xml);

    void Register(const RemoteDocument& doc);
    // Lookup by the exact local path Notepad++ reports for a saved buffer.
    const RemoteDocument* FindByLocalPath(const std::wstring& localPath) const;
    RemoteDocument* FindByLocalPath(const std::wstring& localPath);
    void UpdateRemoteState(const std::wstring& localPath,
                           const std::string& etag,
                           const std::string& lastModified);
    void Remove(const std::wstring& localPath);
    std::vector<RemoteDocument> All() const;

    // Case-insensitive canonical form used as the map key.
    static std::wstring NormalizePath(const std::wstring& path);

private:
    std::wstring m_storeFile;
    std::map<std::wstring, RemoteDocument> m_docs; // key: normalized local path
};

} // namespace npps3
