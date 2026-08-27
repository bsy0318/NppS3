// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "S3Types.h"
#include "StorageError.h"

#include <atomic>
#include <functional>
#include <string>

namespace npps3 {

// Called from the worker thread performing the transfer. total==0 when unknown.
// Returning false requests cancellation of the operation.
using ProgressFn = std::function<bool(uint64_t transferred, uint64_t total)>;

// Synchronous storage operations. All calls are blocking and must run on a
// worker thread, never on the Notepad++ UI thread. Implementations must be
// safe for concurrent calls from multiple threads.
class IObjectStorage
{
public:
    virtual ~IObjectStorage() = default;

    virtual Outcome<std::vector<BucketInfo>> ListBuckets() = 0;

    // delimiter may be empty for a flat listing. continuationToken empty on first page.
    virtual Outcome<ListObjectsResult> ListObjects(const std::string& bucket,
                                                   const std::string& prefix,
                                                   const std::string& delimiter,
                                                   const std::string& continuationToken,
                                                   int maxKeys) = 0;

    virtual Outcome<ObjectMetadata> HeadObject(const std::string& bucket,
                                               const std::string& key) = 0;

    // Downloads to localPath via a temp file; localPath is only replaced on success.
    virtual Outcome<ObjectMetadata> DownloadObject(const std::string& bucket,
                                              const std::string& key,
                                              const std::wstring& localPath,
                                              const ProgressFn& progress) = 0;

    virtual Outcome<PutObjectResult> PutObject(const std::string& bucket,
                                               const std::string& key,
                                               const std::wstring& localPath,
                                               const std::string& contentType,
                                               const ProgressFn& progress) = 0;

    // For zero/small in-memory payloads (new empty file, folder markers).
    virtual Outcome<PutObjectResult> PutObjectBytes(const std::string& bucket,
                                                    const std::string& key,
                                                    const std::string& data,
                                                    const std::string& contentType) = 0;

    virtual VoidResult DeleteObject(const std::string& bucket, const std::string& key) = 0;

    virtual Outcome<PutObjectResult> CopyObject(const std::string& srcBucket,
                                                const std::string& srcKey,
                                                const std::string& dstBucket,
                                                const std::string& dstKey) = 0;

    // Cheap authenticated call used to validate credentials/endpoint.
    virtual VoidResult TestConnection(const std::string& bucket) = 0;

    // ------------------------------------------------------------- multipart
    // Raw S3 multipart operations. PutObject drives these automatically for
    // large files; they are exposed so resume and cleanup can be orchestrated
    // and tested independently.

    virtual Outcome<std::string> CreateMultipartUpload(const std::string& bucket,
                                                       const std::string& key,
                                                       const std::string& contentType) = 0;

    // Sends [offset, offset+length) of localPath as part `partNumber`.
    virtual Outcome<MultipartPart> UploadPart(const std::string& bucket,
                                              const std::string& key,
                                              const std::string& uploadId,
                                              int partNumber,
                                              const std::wstring& localPath,
                                              uint64_t offset,
                                              uint64_t length,
                                              const ProgressFn& progress) = 0;

    virtual Outcome<PutObjectResult> CompleteMultipartUpload(
        const std::string& bucket, const std::string& key, const std::string& uploadId,
        const std::vector<MultipartPart>& parts) = 0;

    virtual VoidResult AbortMultipartUpload(const std::string& bucket,
                                            const std::string& key,
                                            const std::string& uploadId) = 0;

    // All parts already stored for an upload id (pagination handled inside).
    virtual Outcome<std::vector<MultipartPart>> ListParts(const std::string& bucket,
                                                          const std::string& key,
                                                          const std::string& uploadId) = 0;

    // Uploads started but never completed or aborted, under `prefix`.
    virtual Outcome<std::vector<MultipartUploadInfo>> ListMultipartUploads(
        const std::string& bucket, const std::string& prefix) = 0;
};

} // namespace npps3
