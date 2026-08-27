// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "IObjectStorage.h"
#include "UploadJournal.h"

namespace npps3 {

// S3 multipart limits. Cloudflare R2 additionally requires every part except
// the last to be the same size, which is why the plan below is uniform.
constexpr uint64_t kMinPartSize = 5ull * 1024 * 1024;
constexpr uint64_t kMaxPartSize = 5ull * 1024 * 1024 * 1024;
constexpr int kMaxParts = 10000;

struct MultipartPlan
{
    uint64_t partSize = 0;
    uint64_t partCount = 0;
};

// Uniform part layout for `fileSize`. Grows the part size when the desired one
// would need more than maxParts parts. Fails only when the object exceeds what
// maxParts * kMaxPartSize can cover.
Outcome<MultipartPlan> PlanMultipartUpload(uint64_t fileSize, uint64_t desiredPartSize,
                                           int maxParts = kMaxParts);

// Byte range of one 1-based part in a plan.
uint64_t PartOffset(const MultipartPlan& plan, int partNumber);
uint64_t PartLength(const MultipartPlan& plan, uint64_t fileSize, int partNumber);

// Drives create → upload parts → complete, resuming from the journal when an
// earlier attempt for the same object and file was interrupted.
//
// On failure or cancellation the server-side upload and the journal entry are
// deliberately left in place so the next attempt can continue where this one
// stopped. Only a local file that no longer matches the journal causes the
// stale upload to be aborted.
class MultipartUploader
{
public:
    MultipartUploader(IObjectStorage& storage, IUploadJournal* journal, std::string scope)
        : m_storage(storage), m_journal(journal), m_scope(std::move(scope)) {}

    Outcome<PutObjectResult> Upload(const std::string& bucket,
                                    const std::string& key,
                                    const std::wstring& localPath,
                                    const std::string& contentType,
                                    uint64_t fileSize,
                                    unsigned long long fileTime,
                                    const MultipartPlan& plan,
                                    const ProgressFn& progress);

    // True when the last Upload() continued a previously interrupted transfer.
    bool Resumed() const { return m_resumed; }
    uint64_t ResumedBytes() const { return m_resumedBytes; }

private:
    // Returns an upload id to continue, or an empty string to start fresh.
    std::string ResolveResume(const std::string& bucket, const std::string& key,
                              uint64_t fileSize, unsigned long long fileTime,
                              const MultipartPlan& plan,
                              std::vector<MultipartPart>& doneParts);

    IObjectStorage& m_storage;
    IUploadJournal* m_journal;
    std::string m_scope;
    bool m_resumed = false;
    uint64_t m_resumedBytes = 0;
};

} // namespace npps3
