// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace npps3 {

enum class ErrorKind
{
    None = 0,
    Network,        // DNS/TCP/TLS/timeout — usually retryable
    Http,           // non-2xx without a recognized S3 code
    AccessDenied,
    InvalidCredentials, // InvalidAccessKeyId / SignatureDoesNotMatch
    NoSuchBucket,
    NoSuchKey,
    Conflict,       // precondition failed / remote changed
    Throttled,      // 429 / SlowDown
    Cancelled,
    LocalIo,        // local file read/write failure
    Internal,
};

struct StorageError
{
    ErrorKind kind = ErrorKind::None;
    int httpStatus = 0;
    std::string s3Code;      // e.g. "NoSuchKey"; empty when not an S3 XML error
    std::string message;     // safe for display; must never contain credentials
    unsigned long win32 = 0; // WinHTTP/Win32 error code when kind == Network/LocalIo

    bool Retryable() const
    {
        switch (kind)
        {
        case ErrorKind::Network:
        case ErrorKind::Throttled:
            return true;
        case ErrorKind::Http:
            return httpStatus >= 500;
        default:
            return false;
        }
    }

    std::string Describe() const;
};

// Maps an HTTP status plus optional S3 error code to an ErrorKind.
ErrorKind ClassifyS3Error(int httpStatus, const std::string& s3Code);

template <typename T>
struct Outcome
{
    bool ok = false;
    T value{};
    StorageError error{};

    static Outcome Success(T v) { return Outcome{true, std::move(v), {}}; }
    static Outcome Failure(StorageError e) { return Outcome{false, T{}, std::move(e)}; }
};

struct VoidResult
{
    bool ok = false;
    StorageError error{};

    static VoidResult Success() { return VoidResult{true, {}}; }
    static VoidResult Failure(StorageError e) { return VoidResult{false, std::move(e)}; }
};

} // namespace npps3
