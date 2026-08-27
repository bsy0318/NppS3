// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SigV4.h"

#include "../util/Hash.h"
#include "../util/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>

namespace npps3 {

const char* const kEmptyPayloadSha256 =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

std::string SigV4CanonicalRequest(const SigV4Request& req, std::string* signedHeadersOut)
{
    // Canonical query: names/values URI-encoded strictly, sorted by encoded name then value.
    std::vector<std::pair<std::string, std::string>> encoded;
    encoded.reserve(req.query.size());
    for (const auto& [k, v] : req.query)
        encoded.emplace_back(UriEncode(k, false), UriEncode(v, false));
    std::sort(encoded.begin(), encoded.end());
    std::string canonicalQuery;
    for (const auto& [k, v] : encoded)
    {
        if (!canonicalQuery.empty())
            canonicalQuery.push_back('&');
        canonicalQuery += k;
        canonicalQuery.push_back('=');
        canonicalQuery += v;
    }

    // Canonical headers: lowercase names, trimmed values, sorted.
    std::vector<std::pair<std::string, std::string>> hdrs;
    hdrs.reserve(req.headers.size());
    for (const auto& [k, v] : req.headers)
        hdrs.emplace_back(ToLowerAscii(k), Trim(v));
    std::sort(hdrs.begin(), hdrs.end());

    std::string canonicalHeaders;
    std::string signedHeaders;
    for (const auto& [k, v] : hdrs)
    {
        canonicalHeaders += k;
        canonicalHeaders.push_back(':');
        canonicalHeaders += v;
        canonicalHeaders.push_back('\n');
        if (!signedHeaders.empty())
            signedHeaders.push_back(';');
        signedHeaders += k;
    }
    if (signedHeadersOut)
        *signedHeadersOut = signedHeaders;

    std::string out;
    out.reserve(256);
    out += req.method;
    out.push_back('\n');
    out += req.canonicalUri.empty() ? "/" : req.canonicalUri;
    out.push_back('\n');
    out += canonicalQuery;
    out.push_back('\n');
    out += canonicalHeaders;
    out.push_back('\n');
    out += signedHeaders;
    out.push_back('\n');
    out += req.payloadHash;
    return out;
}

std::string SigV4Scope(const std::string& amzDate, const SigV4Credentials& creds)
{
    std::string date = amzDate.substr(0, 8);
    return date + "/" + creds.region + "/" + creds.service + "/aws4_request";
}

std::string SigV4StringToSign(const std::string& canonicalRequest,
                              const std::string& amzDate,
                              const std::string& scope)
{
    std::string out = "AWS4-HMAC-SHA256\n";
    out += amzDate;
    out.push_back('\n');
    out += scope;
    out.push_back('\n');
    out += Sha256Hex(canonicalRequest);
    return out;
}

std::string SigV4Signature(const std::string& stringToSign,
                           const std::string& amzDate,
                           const SigV4Credentials& creds)
{
    std::string kSecret = "AWS4" + creds.secretAccessKey;
    Sha256Digest kDate = HmacSha256(kSecret.data(), kSecret.size(), amzDate.substr(0, 8));
    Sha256Digest kRegion = HmacSha256(kDate.data(), kDate.size(), creds.region);
    Sha256Digest kService = HmacSha256(kRegion.data(), kRegion.size(), creds.service);
    Sha256Digest kSigning = HmacSha256(kService.data(), kService.size(), "aws4_request");
    Sha256Digest sig = HmacSha256(kSigning.data(), kSigning.size(), stringToSign);
    return HexLower(sig.data(), sig.size());
}

std::string SigV4Authorization(const SigV4Request& req,
                               const SigV4Credentials& creds,
                               const std::string& amzDate)
{
    std::string signedHeaders;
    std::string canonical = SigV4CanonicalRequest(req, &signedHeaders);
    std::string scope = SigV4Scope(amzDate, creds);
    std::string stringToSign = SigV4StringToSign(canonical, amzDate, scope);
    std::string signature = SigV4Signature(stringToSign, amzDate, creds);

    std::string out = "AWS4-HMAC-SHA256 Credential=";
    out += creds.accessKeyId;
    out.push_back('/');
    out += scope;
    out += ", SignedHeaders=";
    out += signedHeaders;
    out += ", Signature=";
    out += signature;
    return out;
}

std::string AmzDateNow()
{
    SYSTEMTIME st{};
    ::GetSystemTime(&st);
    char buf[24];
    ::_snprintf_s(buf, _TRUNCATE, "%04u%02u%02uT%02u%02u%02uZ",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

} // namespace npps3
