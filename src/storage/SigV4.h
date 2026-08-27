// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <string>
#include <vector>

namespace npps3 {

// AWS Signature Version 4 request signing.
// Implemented against the official specification and validated by unit tests
// using AWS-published test vectors. Works with any SigV4 service endpoint;
// for S3/R2 the service is "s3" and R2 uses region "auto".

struct SigV4Request
{
    std::string method;                             // "GET", "PUT", ...
    std::string canonicalUri;                       // path, URI-encoded, '/'-separated, must start with '/'
    std::multimap<std::string, std::string> query;  // raw (unencoded) name/value pairs
    std::map<std::string, std::string> headers;     // must include "host"; names case-insensitive on input
    std::string payloadHash;                        // lowercase hex SHA-256, or "UNSIGNED-PAYLOAD"
};

struct SigV4Credentials
{
    std::string accessKeyId;
    std::string secretAccessKey;
    std::string region;
    std::string service; // "s3"
};

// amzDate: "yyyymmddThhmmssZ". Returns the Authorization header value.
// Exposed pieces are separated for testability.
std::string SigV4CanonicalRequest(const SigV4Request& req, std::string* signedHeadersOut);
std::string SigV4StringToSign(const std::string& canonicalRequest,
                              const std::string& amzDate,
                              const std::string& scope);
std::string SigV4Scope(const std::string& amzDate, const SigV4Credentials& creds);
std::string SigV4Signature(const std::string& stringToSign,
                           const std::string& amzDate,
                           const SigV4Credentials& creds);
std::string SigV4Authorization(const SigV4Request& req,
                               const SigV4Credentials& creds,
                               const std::string& amzDate);

// Current UTC time formatted as "yyyymmddThhmmssZ".
std::string AmzDateNow();

// Empty-payload SHA-256 constant.
extern const char* const kEmptyPayloadSha256;

} // namespace npps3
