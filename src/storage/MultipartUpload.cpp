// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MultipartUpload.h"

#include <algorithm>

namespace npps3 {
namespace {

constexpr uint64_t kMiB = 1024ull * 1024;

StorageError InternalError(std::string message)
{
    StorageError e;
    e.kind = ErrorKind::Internal;
    e.message = std::move(message);
    return e;
}

} // namespace

Outcome<MultipartPlan> PlanMultipartUpload(uint64_t fileSize, uint64_t desiredPartSize,
                                           int maxParts)
{
    if (maxParts <= 0)
        maxParts = kMaxParts;
    if (fileSize == 0)
        return Outcome<MultipartPlan>::Failure(
            InternalError("Multipart upload requires a non-empty object"));

    uint64_t part = std::max<uint64_t>(desiredPartSize, kMinPartSize);

    // Grow the part size when the desired one would exceed the part-count cap.
    const uint64_t needed = (fileSize + static_cast<uint64_t>(maxParts) - 1) /
                            static_cast<uint64_t>(maxParts);
    if (needed > part)
        part = ((needed + kMiB - 1) / kMiB) * kMiB;

    if (part > kMaxPartSize)
    {
        const uint64_t limitGiB = static_cast<uint64_t>(maxParts) * (kMaxPartSize / (1024 * kMiB));
        return Outcome<MultipartPlan>::Failure(InternalError(
            "Object is too large for multipart upload (limit " +
            std::to_string(limitGiB) + " GiB)"));
    }

    MultipartPlan plan;
    plan.partSize = part;
    plan.partCount = (fileSize + part - 1) / part;
    return Outcome<MultipartPlan>::Success(plan);
}

uint64_t PartOffset(const MultipartPlan& plan, int partNumber)
{
    return static_cast<uint64_t>(partNumber - 1) * plan.partSize;
}

uint64_t PartLength(const MultipartPlan& plan, uint64_t fileSize, int partNumber)
{
    const uint64_t offset = PartOffset(plan, partNumber);
    if (offset >= fileSize)
        return 0;
    return std::min<uint64_t>(plan.partSize, fileSize - offset);
}

std::string MultipartUploader::ResolveResume(const std::string& bucket, const std::string& key,
                                             uint64_t fileSize, unsigned long long fileTime,
                                             const MultipartPlan& plan,
                                             std::vector<MultipartPart>& doneParts)
{
    doneParts.clear();
    if (!m_journal)
        return {};

    auto entry = m_journal->Find(m_scope, bucket, key);
    if (!entry)
        return {};

    // A changed local file invalidates every part already uploaded; the old
    // upload can never complete into the right object, so drop it now rather
    // than leaving it to accrue storage cost.
    if (entry->fileSize != fileSize || entry->fileTime != fileTime ||
        entry->partSize != plan.partSize)
    {
        m_storage.AbortMultipartUpload(bucket, key, entry->uploadId);
        m_journal->Remove(m_scope, bucket, key);
        return {};
    }

    auto remote = m_storage.ListParts(bucket, key, entry->uploadId);
    if (!remote.ok)
    {
        // The upload id expired or was aborted elsewhere; start over.
        m_journal->Remove(m_scope, bucket, key);
        return {};
    }

    // Trust a part only when the journal and the service agree on it.
    for (const MultipartPart& journalled : entry->parts)
    {
        const uint64_t expected = PartLength(plan, fileSize, journalled.partNumber);
        if (journalled.size != expected)
            continue;
        auto it = std::find_if(remote.value.begin(), remote.value.end(),
                               [&](const MultipartPart& p) {
                                   return p.partNumber == journalled.partNumber &&
                                          p.size == journalled.size &&
                                          p.etag == journalled.etag;
                               });
        if (it != remote.value.end())
            doneParts.push_back(journalled);
    }
    return entry->uploadId;
}

Outcome<PutObjectResult> MultipartUploader::Upload(const std::string& bucket,
                                                   const std::string& key,
                                                   const std::wstring& localPath,
                                                   const std::string& contentType,
                                                   uint64_t fileSize,
                                                   unsigned long long fileTime,
                                                   const MultipartPlan& plan,
                                                   const ProgressFn& progress)
{
    m_resumed = false;
    m_resumedBytes = 0;

    std::vector<MultipartPart> doneParts;
    std::string uploadId = ResolveResume(bucket, key, fileSize, fileTime, plan, doneParts);
    if (!uploadId.empty() && !doneParts.empty())
    {
        m_resumed = true;
        for (const MultipartPart& p : doneParts)
            m_resumedBytes += p.size;
    }

    if (uploadId.empty())
    {
        auto created = m_storage.CreateMultipartUpload(bucket, key, contentType);
        if (!created.ok)
            return Outcome<PutObjectResult>::Failure(created.error);
        uploadId = created.value;

        if (m_journal)
        {
            UploadJournalEntry entry;
            entry.scope = m_scope;
            entry.bucket = bucket;
            entry.key = key;
            entry.uploadId = uploadId;
            entry.localPath = localPath;
            entry.fileSize = fileSize;
            entry.fileTime = fileTime;
            entry.partSize = plan.partSize;
            m_journal->Begin(entry);
        }
    }

    std::vector<MultipartPart> parts;
    parts.reserve(static_cast<size_t>(plan.partCount));
    uint64_t transferred = 0;

    for (int number = 1; number <= static_cast<int>(plan.partCount); ++number)
    {
        const uint64_t offset = PartOffset(plan, number);
        const uint64_t length = PartLength(plan, fileSize, number);
        if (length == 0)
            break;

        auto done = std::find_if(doneParts.begin(), doneParts.end(),
                                 [&](const MultipartPart& p) { return p.partNumber == number; });
        if (done != doneParts.end())
        {
            parts.push_back(*done);
            transferred += length;
            if (progress && !progress(transferred, fileSize))
            {
                StorageError e;
                e.kind = ErrorKind::Cancelled;
                e.message = "Cancelled";
                return Outcome<PutObjectResult>::Failure(e);
            }
            continue;
        }

        const uint64_t base = transferred;
        ProgressFn partProgress;
        if (progress)
            partProgress = [&](uint64_t sent, uint64_t) { return progress(base + sent, fileSize); };

        auto uploaded = m_storage.UploadPart(bucket, key, uploadId, number, localPath,
                                             offset, length, partProgress);
        if (!uploaded.ok)
        {
            // Keep the upload id and the journal: the next attempt resumes here.
            return Outcome<PutObjectResult>::Failure(uploaded.error);
        }

        MultipartPart part = uploaded.value;
        part.partNumber = number;
        part.size = length;
        parts.push_back(part);
        transferred += length;
        if (m_journal)
            m_journal->RecordPart(m_scope, bucket, key, part);
    }

    auto completed = m_storage.CompleteMultipartUpload(bucket, key, uploadId, parts);
    if (!completed.ok)
        return completed;

    if (m_journal)
        m_journal->Remove(m_scope, bucket, key);
    return completed;
}

} // namespace npps3
