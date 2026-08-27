// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "HttpClient.h"
#include "IObjectStorage.h"
#include "SigV4.h"
#include "UploadJournal.h"

#include <memory>

namespace npps3 {

struct S3Config
{
    std::string endpoint;        // e.g. "https://<account>.r2.cloudflarestorage.com"
    std::string region = "auto"; // R2 default; AWS uses a real region
    std::string accessKeyId;
    std::string secretAccessKey;
    bool pathStyle = true;       // safest default for custom/S3-compatible endpoints
    int maxRetries = 3;

    // Files at or above the threshold are uploaded with multipart, which also
    // lifts the 4 GiB ceiling of a single HTTP request and makes an
    // interrupted upload resumable. Tunable so tests can exercise multipart
    // without moving gigabytes.
    uint64_t multipartThreshold = 64ull * 1024 * 1024;
    uint64_t multipartPartSize = 16ull * 1024 * 1024;
};

// Generic S3-compatible client (AWS S3, Cloudflare R2, MinIO, ...) over
// WinHTTP + SigV4. No AWS-specific endpoint assumptions.
class S3Client final : public IObjectStorage
{
public:
    explicit S3Client(S3Config config);
    ~S3Client() override;

    Outcome<std::vector<BucketInfo>> ListBuckets() override;
    Outcome<ListObjectsResult> ListObjects(const std::string& bucket,
                                           const std::string& prefix,
                                           const std::string& delimiter,
                                           const std::string& continuationToken,
                                           int maxKeys) override;
    Outcome<ObjectMetadata> HeadObject(const std::string& bucket,
                                       const std::string& key) override;
    Outcome<ObjectMetadata> DownloadObject(const std::string& bucket,
                                      const std::string& key,
                                      const std::wstring& localPath,
                                      const ProgressFn& progress) override;
    Outcome<PutObjectResult> PutObject(const std::string& bucket,
                                       const std::string& key,
                                       const std::wstring& localPath,
                                       const std::string& contentType,
                                       const ProgressFn& progress) override;
    Outcome<PutObjectResult> PutObjectBytes(const std::string& bucket,
                                            const std::string& key,
                                            const std::string& data,
                                            const std::string& contentType) override;
    VoidResult DeleteObject(const std::string& bucket, const std::string& key) override;
    Outcome<PutObjectResult> CopyObject(const std::string& srcBucket,
                                        const std::string& srcKey,
                                        const std::string& dstBucket,
                                        const std::string& dstKey) override;
    VoidResult TestConnection(const std::string& bucket) override;

    Outcome<std::string> CreateMultipartUpload(const std::string& bucket,
                                               const std::string& key,
                                               const std::string& contentType) override;
    Outcome<MultipartPart> UploadPart(const std::string& bucket, const std::string& key,
                                      const std::string& uploadId, int partNumber,
                                      const std::wstring& localPath, uint64_t offset,
                                      uint64_t length, const ProgressFn& progress) override;
    Outcome<PutObjectResult> CompleteMultipartUpload(
        const std::string& bucket, const std::string& key, const std::string& uploadId,
        const std::vector<MultipartPart>& parts) override;
    VoidResult AbortMultipartUpload(const std::string& bucket, const std::string& key,
                                    const std::string& uploadId) override;
    Outcome<std::vector<MultipartPart>> ListParts(const std::string& bucket,
                                                  const std::string& key,
                                                  const std::string& uploadId) override;
    Outcome<std::vector<MultipartUploadInfo>> ListMultipartUploads(
        const std::string& bucket, const std::string& prefix) override;

    // Cancellation flag observed by all subsequent operations of this client
    // instance; TransferManager wires the per-job token here via a wrapper.
    void SetCancelFlag(std::atomic<bool>* cancel) { m_cancel = cancel; }

    // Optional resume journal for multipart uploads. Borrowed; must outlive
    // this client. Without one, large uploads still work but always restart.
    void SetUploadJournal(IUploadJournal* journal) { m_journal = journal; }

    // Identity used to key journal entries for this client's credentials.
    std::string JournalScope() const;

private:
    struct Endpoint
    {
        std::wstring host;
        unsigned short port = 443;
        bool https = true;
        std::string hostHeader; // host[:port] as it appears in signed headers
    };

    struct RequestSpec
    {
        std::string method;
        std::string bucket; // empty for service-level (ListBuckets)
        std::string key;    // raw, un-encoded
        std::multimap<std::string, std::string> query;
        std::map<std::string, std::string> extraHeaders; // added and signed
        std::string payloadHash;
        const std::string* bodyMem = nullptr;
        std::wstring bodyFile;
        uint64_t bodyFileOffset = 0;
        uint64_t bodyFileLength = 0; // 0 = whole file from the offset
        std::wstring sinkFile;
        ProgressFn progress;
    };

    VoidResult Send(const RequestSpec& spec, HttpResponse& resp);
    VoidResult SendWithRetry(const RequestSpec& spec, HttpResponse& resp, bool idempotent);
    StorageError ErrorFromResponse(const HttpResponse& resp) const;
    static ObjectMetadata MetadataFromHeaders(const HttpResponse& resp);
    Outcome<PutObjectResult> PutObjectSingle(const std::string& bucket, const std::string& key,
                                             const std::wstring& localPath,
                                             const std::string& contentType,
                                             const ProgressFn& progress);

    S3Config m_config;
    Endpoint m_endpoint;
    HttpClient m_http;
    std::atomic<bool>* m_cancel = nullptr;
    IUploadJournal* m_journal = nullptr;
};

} // namespace npps3
