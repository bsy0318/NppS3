// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "S3Types.h"
#include "StorageError.h"

#include <optional>
#include <string>
#include <vector>

namespace npps3 {

// Pure parsing of S3 XML response bodies; kept free of network code so the
// pagination/listing logic is unit-testable.

std::optional<std::vector<BucketInfo>> ParseListBuckets(const std::string& xml);
std::optional<ListObjectsResult> ParseListObjectsV2(const std::string& xml);

// Returns Code/Message when the body is an S3 <Error> document.
struct S3ErrorBody
{
    std::string code;
    std::string message;
};
std::optional<S3ErrorBody> ParseErrorBody(const std::string& xml);

// CopyObject success body.
std::optional<std::string> ParseCopyObjectEtag(const std::string& xml);

// ------------------------------------------------------------------ multipart

// <InitiateMultipartUploadResult>: returns the UploadId.
std::optional<std::string> ParseInitiateMultipartUpload(const std::string& xml);

// <ListPartsResult>: parts already stored server-side for an upload id.
std::optional<ListPartsResult> ParseListParts(const std::string& xml);

// <ListMultipartUploadsResult>: uploads started but never completed.
std::optional<ListMultipartUploadsResult> ParseListMultipartUploads(const std::string& xml);

// <CompleteMultipartUploadResult>: returns the final object ETag.
std::optional<std::string> ParseCompleteMultipartUpload(const std::string& xml);

// Request body for CompleteMultipartUpload. Parts must be ordered by number.
std::string BuildCompleteMultipartUploadXml(const std::vector<MultipartPart>& parts);

} // namespace npps3
