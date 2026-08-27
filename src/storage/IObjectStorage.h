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
};

} // namespace npps3
