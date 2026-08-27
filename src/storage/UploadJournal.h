// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "S3Types.h"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace npps3 {

// One interrupted multipart upload. Holds no credential material: the upload
// id is a server-side handle, and `scope` is a one-way digest of the endpoint
// and access key id so entries never resume against a different account.
struct UploadJournalEntry
{
    std::string scope;
    std::string bucket;
    std::string key;
    std::string uploadId;
    std::wstring localPath;
    uint64_t fileSize = 0;
    unsigned long long fileTime = 0; // last-write FILETIME; guards stale resume
    uint64_t partSize = 0;
    std::vector<MultipartPart> parts; // parts confirmed uploaded so far
};

// Records multipart progress so an upload interrupted by a crash, a cancel or
// a lost connection can continue instead of restarting from byte zero.
// Implementations must be safe to call from transfer worker threads.
class IUploadJournal
{
public:
    virtual ~IUploadJournal() = default;

    virtual std::optional<UploadJournalEntry> Find(const std::string& scope,
                                                   const std::string& bucket,
                                                   const std::string& key) const = 0;
    virtual void Begin(const UploadJournalEntry& entry) = 0;
    virtual void RecordPart(const std::string& scope, const std::string& bucket,
                            const std::string& key, const MultipartPart& part) = 0;
    virtual void Remove(const std::string& scope, const std::string& bucket,
                        const std::string& key) = 0;

    // Identity of the credentials/endpoint an entry belongs to.
    static std::string ScopeFor(const std::string& endpoint, const std::string& accessKeyId);
};

// XML-backed journal stored alongside the plugin configuration. Every mutation
// is flushed immediately: the whole point is to survive an abrupt exit.
class FileUploadJournal final : public IUploadJournal
{
public:
    // Loads the file if it exists; a missing or malformed file starts empty.
    void SetFile(const std::wstring& path);

    std::optional<UploadJournalEntry> Find(const std::string& scope,
                                           const std::string& bucket,
                                           const std::string& key) const override;
    void Begin(const UploadJournalEntry& entry) override;
    void RecordPart(const std::string& scope, const std::string& bucket,
                    const std::string& key, const MultipartPart& part) override;
    void Remove(const std::string& scope, const std::string& bucket,
                const std::string& key) override;

    // All entries for a scope, for cleanup UI.
    std::vector<UploadJournalEntry> Entries() const;

    // Serialization is split out so it can be unit-tested without the disk.
    std::string SerializeToXml() const;
    bool DeserializeFromXml(const std::string& xml);

private:
    UploadJournalEntry* FindLocked(const std::string& scope, const std::string& bucket,
                                   const std::string& key);
    void SaveLocked() const;

    mutable std::mutex m_mutex;
    std::wstring m_file;
    std::vector<UploadJournalEntry> m_entries;
};

} // namespace npps3
