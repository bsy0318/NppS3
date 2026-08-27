// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace npps3 {

struct BucketInfo
{
    std::string name;
    std::string creationDate; // ISO 8601 as returned by the service
};

struct ObjectInfo
{
    std::string key;
    uint64_t size = 0;
    std::string lastModified; // ISO 8601
    std::string etag;         // as returned, quotes stripped; not necessarily an MD5
    std::string storageClass;
};

struct ListObjectsResult
{
    std::vector<ObjectInfo> objects;
    std::vector<std::string> commonPrefixes;
    bool isTruncated = false;
    std::string nextContinuationToken;
};

struct ObjectMetadata
{
    uint64_t size = 0;
    std::string etag;
    std::string lastModified;   // HTTP-date from headers
    std::string contentType;
    std::string contentEncoding;
    std::string cacheControl;
    std::string storageClass;
    std::string versionId;
    std::map<std::string, std::string> userMetadata; // x-amz-meta-* without the prefix
};

struct PutObjectResult
{
    std::string etag;
    std::string versionId;
};

// ------------------------------------------------------------------ multipart

struct MultipartPart
{
    int partNumber = 0;   // 1-based, as required by S3
    uint64_t size = 0;
    std::string etag;     // opaque; only compared for equality, never as an MD5
};

struct ListPartsResult
{
    std::vector<MultipartPart> parts;
    bool isTruncated = false;
    int nextPartNumberMarker = 0;
};

struct MultipartUploadInfo
{
    std::string key;
    std::string uploadId;
    std::string initiated; // ISO 8601
};

struct ListMultipartUploadsResult
{
    std::vector<MultipartUploadInfo> uploads;
    bool isTruncated = false;
    std::string nextKeyMarker;
    std::string nextUploadIdMarker;
};

} // namespace npps3
