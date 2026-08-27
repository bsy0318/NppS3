// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "StorageError.h"
#include "IObjectStorage.h"

#include <atomic>
#include <map>
#include <string>
#include <vector>

namespace npps3 {

struct HttpRequest
{
    std::string method = "GET";
    std::wstring host;
    unsigned short port = 443;
    bool https = true;
    std::wstring pathAndQuery;  // fully encoded, starts with '/'
    std::vector<std::pair<std::wstring, std::wstring>> headers;

    // Request body: at most one of these is used.
    const std::string* bodyMem = nullptr; // borrowed; must outlive the call
    std::wstring bodyFile;                // streamed from disk when non-empty

    // Response body sink: written to this file when non-empty and status < 300,
    // otherwise collected in HttpResponse::body (error payloads are small XML).
    std::wstring sinkFile;

    ProgressFn progress;                  // upload or download progress
    std::atomic<bool>* cancel = nullptr;  // observed between I/O chunks
};

struct HttpResponse
{
    int status = 0;
    std::map<std::string, std::string> headers; // names lowercased
    std::string body;                           // only when sinkFile unused or on error
    uint64_t bodyBytes = 0;
};

// Thin synchronous WinHTTP wrapper. TLS is provided by the OS (Schannel);
// no external HTTP/TLS dependency is shipped with the plugin.
class HttpClient
{
public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Blocking; safe to call concurrently from multiple worker threads.
    VoidResult Execute(const HttpRequest& req, HttpResponse& resp);

private:
    void* m_session = nullptr; // HINTERNET
};

} // namespace npps3
