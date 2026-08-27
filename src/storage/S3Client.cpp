// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "S3Client.h"
#include "MultipartUpload.h"
#include "S3Xml.h"

#include "../util/Hash.h"
#include "../util/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>

namespace npps3 {
namespace {

std::string StripQuotes(std::string s)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

// Size and last-write time in one open, so an upload plan and its resume guard
// describe the same snapshot of the file.
bool StatLocalFile(const std::wstring& path, uint64_t& size, unsigned long long& writeTime)
{
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
        return false;
    ULARGE_INTEGER sz{info.nFileSizeLow, info.nFileSizeHigh};
    ULARGE_INTEGER wt{info.ftLastWriteTime.dwLowDateTime, info.ftLastWriteTime.dwHighDateTime};
    size = sz.QuadPart;
    writeTime = wt.QuadPart;
    return true;
}

} // namespace

ErrorKind ClassifyS3Error(int httpStatus, const std::string& s3Code)
{
    if (s3Code == "NoSuchKey")
        return ErrorKind::NoSuchKey;
    if (s3Code == "NoSuchBucket")
        return ErrorKind::NoSuchBucket;
    if (s3Code == "AccessDenied")
        return ErrorKind::AccessDenied;
    if (s3Code == "InvalidAccessKeyId" || s3Code == "SignatureDoesNotMatch" ||
        s3Code == "TokenRefreshRequired" || s3Code == "ExpiredToken")
        return ErrorKind::InvalidCredentials;
    if (s3Code == "SlowDown" || s3Code == "TooManyRequests" || httpStatus == 429)
        return ErrorKind::Throttled;
    if (s3Code == "PreconditionFailed" || httpStatus == 412 || httpStatus == 409)
        return ErrorKind::Conflict;
    if (s3Code == "RequestTimeout")
        return ErrorKind::Network;
    if (httpStatus == 403)
        return ErrorKind::AccessDenied;
    if (httpStatus == 404)
        return ErrorKind::NoSuchKey;
    return ErrorKind::Http;
}

std::string StorageError::Describe() const
{
    std::string out;
    if (!s3Code.empty())
    {
        out = s3Code;
        if (!message.empty())
            out += ": " + message;
    }
    else if (!message.empty())
    {
        out = message;
    }
    else
    {
        out = "Unknown error";
    }
    if (httpStatus > 0)
        out += " (HTTP " + std::to_string(httpStatus) + ")";
    return out;
}

S3Client::S3Client(S3Config config)
    : m_config(std::move(config))
{
    // Accept "https://host[:port]", "http://host[:port]" or a bare host.
    std::string ep = Trim(m_config.endpoint);
    bool https = true;
    if (StartsWith(ep, "https://"))
        ep = ep.substr(8);
    else if (StartsWith(ep, "http://"))
    {
        https = false;
        ep = ep.substr(7);
    }
    while (!ep.empty() && ep.back() == '/')
        ep.pop_back();

    unsigned short port = https ? 443 : 80;
    std::string host = ep;
    size_t colon = ep.rfind(':');
    if (colon != std::string::npos && ep.find(':') == colon) // not IPv6
    {
        host = ep.substr(0, colon);
        int p = ::atoi(ep.c_str() + colon + 1);
        if (p > 0 && p <= 65535)
            port = static_cast<unsigned short>(p);
    }

    m_endpoint.https = https;
    m_endpoint.port = port;
    m_endpoint.host = Utf8ToWide(host);
    m_endpoint.hostHeader = host;
    const bool defaultPort = (https && port == 443) || (!https && port == 80);
    if (!defaultPort)
        m_endpoint.hostHeader += ":" + std::to_string(port);
}

S3Client::~S3Client()
{
    // Best-effort: do not leave the secret lying around in freed memory.
    ::SecureZeroMemory(m_config.secretAccessKey.data(), m_config.secretAccessKey.size());
}

StorageError S3Client::ErrorFromResponse(const HttpResponse& resp) const
{
    StorageError e;
    e.httpStatus = resp.status;

    if (auto body = ParseErrorBody(resp.body))
    {
        e.s3Code = body->code;
        e.message = body->message;
    }
    e.kind = ClassifyS3Error(resp.status, e.s3Code);
    if (e.message.empty())
        e.message = "Request failed";
    return e;
}

ObjectMetadata S3Client::MetadataFromHeaders(const HttpResponse& resp)
{
    ObjectMetadata md;
    auto get = [&](const char* name) -> std::string {
        auto it = resp.headers.find(name);
        return it != resp.headers.end() ? it->second : std::string();
    };
    md.etag = StripQuotes(get("etag"));
    md.lastModified = get("last-modified");
    md.contentType = get("content-type");
    md.contentEncoding = get("content-encoding");
    md.cacheControl = get("cache-control");
    md.storageClass = get("x-amz-storage-class");
    md.versionId = get("x-amz-version-id");
    std::string len = get("content-length");
    if (!len.empty())
        md.size = ::_strtoui64(len.c_str(), nullptr, 10);
    for (const auto& [name, value] : resp.headers)
    {
        if (StartsWith(name, "x-amz-meta-"))
            md.userMetadata[name.substr(11)] = value;
    }
    return md;
}

VoidResult S3Client::Send(const RequestSpec& spec, HttpResponse& resp)
{
    // Canonical URI: path-style prefixes the bucket; virtual-hosted style
    // moves the bucket into the host name.
    std::string canonicalUri = "/";
    std::string hostHeader = m_endpoint.hostHeader;
    std::wstring connectHost = m_endpoint.host;
    if (!spec.bucket.empty())
    {
        if (m_config.pathStyle)
        {
            canonicalUri += UriEncode(spec.bucket, false);
            if (!spec.key.empty())
                canonicalUri += "/" + UriEncode(spec.key, true);
        }
        else
        {
            hostHeader = spec.bucket + "." + m_endpoint.hostHeader;
            connectHost = Utf8ToWide(spec.bucket) + L"." + m_endpoint.host;
            if (!spec.key.empty())
                canonicalUri += UriEncode(spec.key, true);
        }
    }

    const std::string amzDate = AmzDateNow();

    SigV4Request sreq;
    sreq.method = spec.method;
    sreq.canonicalUri = canonicalUri;
    sreq.query = spec.query;
    sreq.payloadHash = spec.payloadHash.empty() ? kEmptyPayloadSha256 : spec.payloadHash;
    sreq.headers["host"] = hostHeader;
    sreq.headers["x-amz-date"] = amzDate;
    sreq.headers["x-amz-content-sha256"] = sreq.payloadHash;
    for (const auto& [k, v] : spec.extraHeaders)
        sreq.headers[ToLowerAscii(k)] = v;

    SigV4Credentials creds{m_config.accessKeyId, m_config.secretAccessKey,
                           m_config.region, "s3"};
    std::string authorization = SigV4Authorization(sreq, creds, amzDate);

    // The wire query string must match the canonical (sorted, encoded) form.
    std::vector<std::pair<std::string, std::string>> encQuery;
    for (const auto& [k, v] : spec.query)
        encQuery.emplace_back(UriEncode(k, false), UriEncode(v, false));
    std::sort(encQuery.begin(), encQuery.end());
    std::string queryString;
    for (const auto& [k, v] : encQuery)
    {
        queryString += queryString.empty() ? "?" : "&";
        queryString += k + "=" + v;
    }

    HttpRequest hreq;
    hreq.method = spec.method;
    hreq.host = connectHost;
    hreq.port = m_endpoint.port;
    hreq.https = m_endpoint.https;
    hreq.pathAndQuery = Utf8ToWide(canonicalUri + queryString);
    hreq.headers.emplace_back(L"x-amz-date", Utf8ToWide(amzDate));
    hreq.headers.emplace_back(L"x-amz-content-sha256", Utf8ToWide(sreq.payloadHash));
    // Authorization contains the signature; it is sent but must never be logged.
    hreq.headers.emplace_back(L"Authorization", Utf8ToWide(authorization));
    for (const auto& [k, v] : spec.extraHeaders)
        hreq.headers.emplace_back(Utf8ToWide(k), Utf8ToWide(v));
    hreq.bodyMem = spec.bodyMem;
    hreq.bodyFile = spec.bodyFile;
    hreq.bodyFileOffset = spec.bodyFileOffset;
    hreq.bodyFileLength = spec.bodyFileLength;
    hreq.sinkFile = spec.sinkFile;
    hreq.progress = spec.progress;
    hreq.cancel = m_cancel;

    return m_http.Execute(hreq, resp);
}

VoidResult S3Client::SendWithRetry(const RequestSpec& spec, HttpResponse& resp, bool idempotent)
{
    int attempts = idempotent ? std::max(1, m_config.maxRetries) : 1;
    VoidResult last = VoidResult::Failure({});
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        if (attempt > 0)
        {
            // Exponential backoff: 500ms, 1s, 2s...
            DWORD delay = 500u << (attempt - 1);
            for (DWORD waited = 0; waited < delay; waited += 100)
            {
                if (m_cancel && m_cancel->load(std::memory_order_relaxed))
                {
                    StorageError e;
                    e.kind = ErrorKind::Cancelled;
                    e.message = "Cancelled";
                    return VoidResult::Failure(e);
                }
                ::Sleep(100);
            }
        }
        last = Send(spec, resp);
        if (!last.ok)
        {
            if (last.error.kind == ErrorKind::Network && last.error.Retryable())
                continue;
            return last;
        }
        if (resp.status >= 200 && resp.status < 300)
            return last;
        StorageError e = ErrorFromResponse(resp);
        if (!e.Retryable())
            return VoidResult::Failure(e);
        last = VoidResult::Failure(e);
    }
    return last;
}

Outcome<std::vector<BucketInfo>> S3Client::ListBuckets()
{
    RequestSpec spec;
    spec.method = "GET";

    HttpResponse resp;
    VoidResult r = SendWithRetry(spec, resp, true);
    if (!r.ok)
        return Outcome<std::vector<BucketInfo>>::Failure(r.error);

    auto parsed = ParseListBuckets(resp.body);
    if (!parsed)
    {
        StorageError e;
        e.kind = ErrorKind::Internal;
        e.detail = ErrorDetail::MalformedResponse;
        e.message = "Malformed ListBuckets response";
        return Outcome<std::vector<BucketInfo>>::Failure(e);
    }
    return Outcome<std::vector<BucketInfo>>::Success(std::move(*parsed));
}

Outcome<ListObjectsResult> S3Client::ListObjects(const std::string& bucket,
                                                 const std::string& prefix,
                                                 const std::string& delimiter,
                                                 const std::string& continuationToken,
                                                 int maxKeys)
{
    RequestSpec spec;
    spec.method = "GET";
    spec.bucket = bucket;
    spec.query.emplace("list-type", "2");
    if (!prefix.empty())
        spec.query.emplace("prefix", prefix);
    if (!delimiter.empty())
        spec.query.emplace("delimiter", delimiter);
    if (!continuationToken.empty())
        spec.query.emplace("continuation-token", continuationToken);
    if (maxKeys > 0)
        spec.query.emplace("max-keys", std::to_string(maxKeys));

    HttpResponse resp;
    VoidResult r = SendWithRetry(spec, resp, true);
    if (!r.ok)
        return Outcome<ListObjectsResult>::Failure(r.error);

    auto parsed = ParseListObjectsV2(resp.body);
    if (!parsed)
    {
        StorageError e;
        e.kind = ErrorKind::Internal;
        e.detail = ErrorDetail::MalformedResponse;
        e.message = "Malformed ListObjectsV2 response";
        return Outcome<ListObjectsResult>::Failure(e);
    }
    return Outcome<ListObjectsResult>::Success(std::move(*parsed));
}

Outcome<ObjectMetadata> S3Client::HeadObject(const std::string& bucket, const std::string& key)
{
    RequestSpec spec;
    spec.method = "HEAD";
    spec.bucket = bucket;
    spec.key = key;

    HttpResponse resp;
    VoidResult r = SendWithRetry(spec, resp, true);
    if (!r.ok)
    {
        // HEAD responses carry no XML body; classify from status alone.
        if (r.error.kind == ErrorKind::Http || r.error.s3Code.empty())
            r.error.kind = ClassifyS3Error(r.error.httpStatus, r.error.s3Code);
        return Outcome<ObjectMetadata>::Failure(r.error);
    }
    return Outcome<ObjectMetadata>::Success(MetadataFromHeaders(resp));
}

Outcome<ObjectMetadata> S3Client::DownloadObject(const std::string& bucket,
                                            const std::string& key,
                                            const std::wstring& localPath,
                                            const ProgressFn& progress)
{
    // Download to a temp file first so an interrupted transfer never
    // masquerades as a completed download.
    std::wstring tmpPath = localPath + L".npps3tmp";

    RequestSpec spec;
    spec.method = "GET";
    spec.bucket = bucket;
    spec.key = key;
    spec.sinkFile = tmpPath;
    spec.progress = progress;

    HttpResponse resp;
    VoidResult r = SendWithRetry(spec, resp, true);
    if (!r.ok)
    {
        ::DeleteFileW(tmpPath.c_str());
        return Outcome<ObjectMetadata>::Failure(r.error);
    }

    ObjectMetadata md = MetadataFromHeaders(resp);
    if (md.size != 0 && resp.bodyBytes != md.size)
    {
        ::DeleteFileW(tmpPath.c_str());
        StorageError e;
        e.kind = ErrorKind::Network;
        e.detail = ErrorDetail::IncompleteDownload;
        e.message = "Incomplete download (received " + std::to_string(resp.bodyBytes) +
                    " of " + std::to_string(md.size) + " bytes)";
        return Outcome<ObjectMetadata>::Failure(e);
    }

    if (!::MoveFileExW(tmpPath.c_str(), localPath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DWORD err = ::GetLastError();
        ::DeleteFileW(tmpPath.c_str());
        StorageError e;
        e.kind = ErrorKind::LocalIo;
        e.win32 = err;
        e.detail = ErrorDetail::LocalMoveFailed;
        e.message = "Could not move downloaded file into place";
        return Outcome<ObjectMetadata>::Failure(e);
    }
    md.size = resp.bodyBytes;
    return Outcome<ObjectMetadata>::Success(std::move(md));
}

std::string S3Client::JournalScope() const
{
    return IUploadJournal::ScopeFor(m_config.endpoint, m_config.accessKeyId);
}

Outcome<PutObjectResult> S3Client::PutObject(const std::string& bucket,
                                             const std::string& key,
                                             const std::wstring& localPath,
                                             const std::string& contentType,
                                             const ProgressFn& progress)
{
    uint64_t fileSize = 0;
    unsigned long long fileTime = 0;
    if (!StatLocalFile(localPath, fileSize, fileTime))
    {
        StorageError e;
        e.kind = ErrorKind::LocalIo;
        e.win32 = ::GetLastError();
        e.detail = ErrorDetail::LocalOpenFailed;
        e.message = "Cannot read local file for upload";
        return Outcome<PutObjectResult>::Failure(e);
    }

    // Small objects stay on the single-request path: it is one round trip and
    // needs no server-side cleanup if it fails.
    if (fileSize < m_config.multipartThreshold || fileSize == 0)
        return PutObjectSingle(bucket, key, localPath, contentType, progress);

    auto plan = PlanMultipartUpload(fileSize, m_config.multipartPartSize);
    if (!plan.ok)
        return Outcome<PutObjectResult>::Failure(plan.error);

    MultipartUploader uploader(*this, m_journal, JournalScope());
    return uploader.Upload(bucket, key, localPath, contentType, fileSize, fileTime,
                           plan.value, progress);
}

Outcome<PutObjectResult> S3Client::PutObjectSingle(const std::string& bucket,
                                                   const std::string& key,
                                                   const std::wstring& localPath,
                                                   const std::string& contentType,
                                                   const ProgressFn& progress)
{
    Sha256Digest digest{};
    if (!Sha256File(localPath, digest))
    {
        StorageError e;
        e.kind = ErrorKind::LocalIo;
        e.win32 = ::GetLastError();
        e.detail = ErrorDetail::LocalOpenFailed;
        e.message = "Cannot read local file for upload";
        return Outcome<PutObjectResult>::Failure(e);
    }

    RequestSpec spec;
    spec.method = "PUT";
    spec.bucket = bucket;
    spec.key = key;
    spec.payloadHash = HexLower(digest.data(), digest.size());
    spec.bodyFile = localPath;
    spec.progress = progress;
    if (!contentType.empty())
        spec.extraHeaders["content-type"] = contentType;

    HttpResponse resp;
    // PUT of a full object is idempotent; safe to retry.
    VoidResult r = SendWithRetry(spec, resp, true);
    if (!r.ok)
        return Outcome<PutObjectResult>::Failure(r.error);

    PutObjectResult out;
    auto it = resp.headers.find("etag");
    if (it != resp.headers.end())
        out.etag = StripQuotes(it->second);
    it = resp.headers.find("x-amz-version-id");
    if (it != resp.headers.end())
        out.versionId = it->second;
    return Outcome<PutObjectResult>::Success(std::move(out));
}

Outcome<PutObjectResult> S3Client::PutObjectBytes(const std::string& bucket,
                                                  const std::string& key,
                                                  const std::string& data,
                                                  const std::string& contentType)
{
    RequestSpec spec;
    spec.method = "PUT";
    spec.bucket = bucket;
    spec.key = key;
    spec.payloadHash = Sha256Hex(data);
    spec.bodyMem = &data;
    if (!contentType.empty())
        spec.extraHeaders["content-type"] = contentType;

    HttpResponse resp;
    VoidResult r = SendWithRetry(spec, resp, true);
    if (!r.ok)
        return Outcome<PutObjectResult>::Failure(r.error);

    PutObjectResult out;
    auto it = resp.headers.find("etag");
    if (it != resp.headers.end())
        out.etag = StripQuotes(it->second);
    return Outcome<PutObjectResult>::Success(std::move(out));
}

VoidResult S3Client::DeleteObject(const std::string& bucket, const std::string& key)
{
    RequestSpec spec;
    spec.method = "DELETE";
    spec.bucket = bucket;
    spec.key = key;

    HttpResponse resp;
    // DELETE is idempotent (deleting an absent key succeeds), so retry is safe.
    return SendWithRetry(spec, resp, true);
}

Outcome<PutObjectResult> S3Client::CopyObject(const std::string& srcBucket,
                                              const std::string& srcKey,
                                              const std::string& dstBucket,
                                              const std::string& dstKey)
{
    RequestSpec spec;
    spec.method = "PUT";
    spec.bucket = dstBucket;
    spec.key = dstKey;
    spec.extraHeaders["x-amz-copy-source"] =
        "/" + UriEncode(srcBucket, false) + "/" + UriEncode(srcKey, true);

    HttpResponse resp;
    VoidResult r = SendWithRetry(spec, resp, true);
    if (!r.ok)
        return Outcome<PutObjectResult>::Failure(r.error);

    // S3 can return HTTP 200 with an <Error> body for CopyObject.
    if (auto err = ParseErrorBody(resp.body))
    {
        StorageError e;
        e.httpStatus = resp.status;
        e.s3Code = err->code;
        e.message = err->message;
        e.kind = ClassifyS3Error(resp.status, e.s3Code);
        return Outcome<PutObjectResult>::Failure(e);
    }

    PutObjectResult out;
    if (auto etag = ParseCopyObjectEtag(resp.body))
        out.etag = *etag;
    return Outcome<PutObjectResult>::Success(std::move(out));
}

VoidResult S3Client::TestConnection(const std::string& bucket)
{
    if (bucket.empty())
    {
        auto r = ListBuckets();
        return r.ok ? VoidResult::Success() : VoidResult::Failure(r.error);
    }
    auto r = ListObjects(bucket, "", "/", "", 1);
    return r.ok ? VoidResult::Success() : VoidResult::Failure(r.error);
}

// ------------------------------------------------------------------ multipart

Outcome<std::string> S3Client::CreateMultipartUpload(const std::string& bucket,
                                                     const std::string& key,
                                                     const std::string& contentType)
{
    RequestSpec spec;
    spec.method = "POST";
    spec.bucket = bucket;
    spec.key = key;
    spec.query.emplace("uploads", "");
    if (!contentType.empty())
        spec.extraHeaders["content-type"] = contentType;

    HttpResponse resp;
    // Retrying would leave an orphan upload behind, so this one is sent once.
    VoidResult r = SendWithRetry(spec, resp, false);
    if (!r.ok)
        return Outcome<std::string>::Failure(r.error);

    auto uploadId = ParseInitiateMultipartUpload(resp.body);
    if (!uploadId)
    {
        StorageError e;
        e.kind = ErrorKind::Internal;
        e.detail = ErrorDetail::MalformedResponse;
        e.message = "Malformed InitiateMultipartUpload response";
        return Outcome<std::string>::Failure(e);
    }
    return Outcome<std::string>::Success(std::move(*uploadId));
}

Outcome<MultipartPart> S3Client::UploadPart(const std::string& bucket, const std::string& key,
                                            const std::string& uploadId, int partNumber,
                                            const std::wstring& localPath, uint64_t offset,
                                            uint64_t length, const ProgressFn& progress)
{
    Sha256Digest digest{};
    if (!Sha256FileRange(localPath, offset, length, digest))
    {
        StorageError e;
        e.kind = ErrorKind::LocalIo;
        e.win32 = ::GetLastError();
        e.detail = ErrorDetail::LocalRangeMissing;
        e.message = "Cannot read local file range for upload";
        return Outcome<MultipartPart>::Failure(e);
    }

    RequestSpec spec;
    spec.method = "PUT";
    spec.bucket = bucket;
    spec.key = key;
    spec.query.emplace("partNumber", std::to_string(partNumber));
    spec.query.emplace("uploadId", uploadId);
    spec.payloadHash = HexLower(digest.data(), digest.size());
    spec.bodyFile = localPath;
    spec.bodyFileOffset = offset;
    spec.bodyFileLength = length;
    spec.progress = progress;

    HttpResponse resp;
    // Re-sending the same part number overwrites it, so retries are safe.
    VoidResult r = SendWithRetry(spec, resp, true);
    if (!r.ok)
        return Outcome<MultipartPart>::Failure(r.error);

    MultipartPart part;
    part.partNumber = partNumber;
    part.size = length;
    auto it = resp.headers.find("etag");
    if (it != resp.headers.end())
        part.etag = StripQuotes(it->second);
    if (part.etag.empty())
    {
        StorageError e;
        e.kind = ErrorKind::Internal;
        e.detail = ErrorDetail::MissingPartEtag;
        e.message = "UploadPart response carried no ETag";
        return Outcome<MultipartPart>::Failure(e);
    }
    return Outcome<MultipartPart>::Success(std::move(part));
}

Outcome<PutObjectResult> S3Client::CompleteMultipartUpload(
    const std::string& bucket, const std::string& key, const std::string& uploadId,
    const std::vector<MultipartPart>& parts)
{
    const std::string body = BuildCompleteMultipartUploadXml(parts);

    RequestSpec spec;
    spec.method = "POST";
    spec.bucket = bucket;
    spec.key = key;
    spec.query.emplace("uploadId", uploadId);
    spec.payloadHash = Sha256Hex(body);
    spec.bodyMem = &body;
    spec.extraHeaders["content-type"] = "application/xml";

    HttpResponse resp;
    VoidResult r = SendWithRetry(spec, resp, false);
    if (!r.ok)
        return Outcome<PutObjectResult>::Failure(r.error);

    // Like CopyObject, this can answer HTTP 200 with an <Error> body because
    // the service starts streaming before the assembly finishes.
    if (auto err = ParseErrorBody(resp.body))
    {
        StorageError e;
        e.httpStatus = resp.status;
        e.s3Code = err->code;
        e.message = err->message;
        e.kind = ClassifyS3Error(resp.status, e.s3Code);
        return Outcome<PutObjectResult>::Failure(e);
    }

    PutObjectResult out;
    if (auto etag = ParseCompleteMultipartUpload(resp.body))
        out.etag = *etag;
    auto it = resp.headers.find("x-amz-version-id");
    if (it != resp.headers.end())
        out.versionId = it->second;
    return Outcome<PutObjectResult>::Success(std::move(out));
}

VoidResult S3Client::AbortMultipartUpload(const std::string& bucket, const std::string& key,
                                          const std::string& uploadId)
{
    RequestSpec spec;
    spec.method = "DELETE";
    spec.bucket = bucket;
    spec.key = key;
    spec.query.emplace("uploadId", uploadId);

    HttpResponse resp;
    return SendWithRetry(spec, resp, true);
}

Outcome<std::vector<MultipartPart>> S3Client::ListParts(const std::string& bucket,
                                                        const std::string& key,
                                                        const std::string& uploadId)
{
    std::vector<MultipartPart> all;
    int marker = 0;
    for (int page = 0; page < 200; ++page) // 200 * 1000 parts covers the 10k cap
    {
        RequestSpec spec;
        spec.method = "GET";
        spec.bucket = bucket;
        spec.key = key;
        spec.query.emplace("uploadId", uploadId);
        spec.query.emplace("max-parts", "1000");
        if (marker > 0)
            spec.query.emplace("part-number-marker", std::to_string(marker));

        HttpResponse resp;
        VoidResult r = SendWithRetry(spec, resp, true);
        if (!r.ok)
            return Outcome<std::vector<MultipartPart>>::Failure(r.error);

        auto parsed = ParseListParts(resp.body);
        if (!parsed)
        {
            StorageError e;
            e.kind = ErrorKind::Internal;
            e.detail = ErrorDetail::MalformedResponse;
            e.message = "Malformed ListParts response";
            return Outcome<std::vector<MultipartPart>>::Failure(e);
        }
        all.insert(all.end(), parsed->parts.begin(), parsed->parts.end());
        if (!parsed->isTruncated || parsed->nextPartNumberMarker <= marker)
            break;
        marker = parsed->nextPartNumberMarker;
    }
    return Outcome<std::vector<MultipartPart>>::Success(std::move(all));
}

Outcome<std::vector<MultipartUploadInfo>> S3Client::ListMultipartUploads(
    const std::string& bucket, const std::string& prefix)
{
    std::vector<MultipartUploadInfo> all;
    std::string keyMarker;
    std::string uploadIdMarker;
    for (int page = 0; page < 100; ++page)
    {
        RequestSpec spec;
        spec.method = "GET";
        spec.bucket = bucket;
        spec.query.emplace("uploads", "");
        spec.query.emplace("max-uploads", "1000");
        if (!prefix.empty())
            spec.query.emplace("prefix", prefix);
        if (!keyMarker.empty())
            spec.query.emplace("key-marker", keyMarker);
        if (!uploadIdMarker.empty())
            spec.query.emplace("upload-id-marker", uploadIdMarker);

        HttpResponse resp;
        VoidResult r = SendWithRetry(spec, resp, true);
        if (!r.ok)
            return Outcome<std::vector<MultipartUploadInfo>>::Failure(r.error);

        auto parsed = ParseListMultipartUploads(resp.body);
        if (!parsed)
        {
            StorageError e;
            e.kind = ErrorKind::Internal;
            e.detail = ErrorDetail::MalformedResponse;
            e.message = "Malformed ListMultipartUploads response";
            return Outcome<std::vector<MultipartUploadInfo>>::Failure(e);
        }
        all.insert(all.end(), parsed->uploads.begin(), parsed->uploads.end());
        if (!parsed->isTruncated || parsed->nextKeyMarker.empty())
            break;
        keyMarker = parsed->nextKeyMarker;
        uploadIdMarker = parsed->nextUploadIdMarker;
    }
    return Outcome<std::vector<MultipartUploadInfo>>::Success(std::move(all));
}

} // namespace npps3
