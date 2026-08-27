// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HttpClient.h"

#include "../util/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>

#pragma comment(lib, "winhttp.lib")

namespace npps3 {
namespace {

constexpr DWORD kChunkSize = 64 * 1024;
constexpr uint64_t kMaxMemoryBody = 64ull * 1024 * 1024; // sanity cap for in-memory bodies

struct HandleGuard
{
    HINTERNET h = nullptr;
    ~HandleGuard() { if (h) ::WinHttpCloseHandle(h); }
};

StorageError NetworkError(const char* stage, DWORD err)
{
    StorageError e;
    e.kind = ErrorKind::Network;
    e.win32 = err;
    e.message = std::string("Network error during ") + stage + " (code " + std::to_string(err) + ")";
    if (err == ERROR_WINHTTP_TIMEOUT)
        e.message = std::string("Timeout during ") + stage;
    else if (err == ERROR_WINHTTP_NAME_NOT_RESOLVED)
        e.message = "DNS name could not be resolved";
    else if (err == ERROR_WINHTTP_CANNOT_CONNECT)
        e.message = "Connection could not be established";
    else if (err == ERROR_WINHTTP_SECURE_FAILURE)
        e.message = "TLS certificate/security failure";
    return e;
}

StorageError CancelledError()
{
    StorageError e;
    e.kind = ErrorKind::Cancelled;
    e.message = "Cancelled";
    return e;
}

bool IsCancelled(const HttpRequest& req)
{
    return req.cancel && req.cancel->load(std::memory_order_relaxed);
}

class FileReader
{
public:
    bool Open(const std::wstring& path)
    {
        m_file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (m_file == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER sz{};
        if (!::GetFileSizeEx(m_file, &sz))
            return false;
        m_size = static_cast<uint64_t>(sz.QuadPart);
        return true;
    }
    bool Seek(uint64_t offset)
    {
        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(offset);
        return ::SetFilePointerEx(m_file, pos, nullptr, FILE_BEGIN) != 0;
    }
    ~FileReader() { if (m_file != INVALID_HANDLE_VALUE) ::CloseHandle(m_file); }
    uint64_t Size() const { return m_size; }
    bool Read(void* buf, DWORD want, DWORD& got)
    {
        return ::ReadFile(m_file, buf, want, &got, nullptr) != 0;
    }

private:
    HANDLE m_file = INVALID_HANDLE_VALUE;
    uint64_t m_size = 0;
};

class FileWriter
{
public:
    bool Open(const std::wstring& path)
    {
        m_file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return m_file != INVALID_HANDLE_VALUE;
    }
    ~FileWriter() { Close(); }
    bool Write(const void* buf, DWORD len)
    {
        DWORD written = 0;
        return ::WriteFile(m_file, buf, len, &written, nullptr) && written == len;
    }
    bool Close()
    {
        if (m_file == INVALID_HANDLE_VALUE)
            return true;
        BOOL ok = ::CloseHandle(m_file);
        m_file = INVALID_HANDLE_VALUE;
        return ok != 0;
    }

private:
    HANDLE m_file = INVALID_HANDLE_VALUE;
};

} // namespace

HttpClient::HttpClient()
{
    // Automatic proxy resolution follows the user's system configuration.
    m_session = ::WinHttpOpen(L"NppS3/1.0",
                              WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!m_session)
    {
        // Older systems fall back to default proxy discovery.
        m_session = ::WinHttpOpen(L"NppS3/1.0",
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (m_session)
        ::WinHttpSetTimeouts(static_cast<HINTERNET>(m_session), 15000, 30000, 60000, 60000);
}

HttpClient::~HttpClient()
{
    if (m_session)
        ::WinHttpCloseHandle(static_cast<HINTERNET>(m_session));
}

VoidResult HttpClient::Execute(const HttpRequest& req, HttpResponse& resp)
{
    resp = HttpResponse{};
    if (!m_session)
        return VoidResult::Failure(NetworkError("session init", ::GetLastError()));
    if (IsCancelled(req))
        return VoidResult::Failure(CancelledError());

    HandleGuard connect;
    connect.h = ::WinHttpConnect(static_cast<HINTERNET>(m_session), req.host.c_str(), req.port, 0);
    if (!connect.h)
        return VoidResult::Failure(NetworkError("connect", ::GetLastError()));

    std::wstring method = Utf8ToWide(req.method);
    HandleGuard request;
    request.h = ::WinHttpOpenRequest(connect.h, method.c_str(), req.pathAndQuery.c_str(),
                                     nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     req.https ? WINHTTP_FLAG_SECURE : 0);
    if (!request.h)
        return VoidResult::Failure(NetworkError("open request", ::GetLastError()));

    // Redirects would break SigV4 (signature covers host/path); surface them instead.
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    ::WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));

    std::wstring headerBlock;
    for (const auto& [name, value] : req.headers)
    {
        headerBlock += name;
        headerBlock += L": ";
        headerBlock += value;
        headerBlock += L"\r\n";
    }
    if (!headerBlock.empty())
    {
        if (!::WinHttpAddRequestHeaders(request.h, headerBlock.c_str(),
                                        static_cast<DWORD>(headerBlock.size()),
                                        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
            return VoidResult::Failure(NetworkError("add headers", ::GetLastError()));
    }

    // Determine body length up front (WinHTTP needs the total for non-chunked sends).
    FileReader fileBody;
    uint64_t bodyLen = 0;
    if (!req.bodyFile.empty())
    {
        if (!fileBody.Open(req.bodyFile))
        {
            StorageError e;
            e.kind = ErrorKind::LocalIo;
            e.win32 = ::GetLastError();
            e.message = "Cannot open local file for upload";
            return VoidResult::Failure(e);
        }
        const uint64_t fileSize = fileBody.Size();
        if (req.bodyFileOffset > fileSize)
        {
            StorageError e;
            e.kind = ErrorKind::LocalIo;
            e.message = "Local file is shorter than the requested upload range";
            return VoidResult::Failure(e);
        }
        const uint64_t available = fileSize - req.bodyFileOffset;
        bodyLen = req.bodyFileLength == 0 ? available
                                          : std::min<uint64_t>(req.bodyFileLength, available);
        if (req.bodyFileOffset != 0 && !fileBody.Seek(req.bodyFileOffset))
        {
            StorageError e;
            e.kind = ErrorKind::LocalIo;
            e.win32 = ::GetLastError();
            e.message = "Cannot seek to the requested upload range";
            return VoidResult::Failure(e);
        }
    }
    else if (req.bodyMem)
    {
        bodyLen = req.bodyMem->size();
    }

    // WinHttpSendRequest takes a DWORD content length, so a single request can
    // never carry 4 GiB. Larger objects go through multipart upload, which
    // splits them into parts far below this bound.
    if (bodyLen > 0xFFFFFFFFull)
    {
        StorageError e;
        e.kind = ErrorKind::Internal;
        e.message = "Request body exceeds the 4 GiB limit of a single HTTP request";
        return VoidResult::Failure(e);
    }

    if (!::WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0,
                              static_cast<DWORD>(bodyLen), 0))
        return VoidResult::Failure(NetworkError("send request", ::GetLastError()));

    // Stream the body in chunks so cancellation and progress stay responsive.
    if (bodyLen > 0)
    {
        uint64_t sent = 0;
        if (!req.bodyFile.empty())
        {
            std::vector<unsigned char> buf(kChunkSize);
            while (sent < bodyLen)
            {
                if (IsCancelled(req))
                    return VoidResult::Failure(CancelledError());
                DWORD want = static_cast<DWORD>(std::min<uint64_t>(kChunkSize, bodyLen - sent));
                DWORD got = 0;
                if (!fileBody.Read(buf.data(), want, got))
                {
                    StorageError e;
                    e.kind = ErrorKind::LocalIo;
                    e.win32 = ::GetLastError();
                    e.message = "Local file read failed during upload";
                    return VoidResult::Failure(e);
                }
                if (got == 0)
                    break; // file shrank underneath us; server will reject the short body
                DWORD written = 0;
                if (!::WinHttpWriteData(request.h, buf.data(), got, &written))
                    return VoidResult::Failure(NetworkError("upload", ::GetLastError()));
                sent += written;
                if (req.progress && !req.progress(sent, bodyLen))
                    return VoidResult::Failure(CancelledError());
            }
        }
        else
        {
            const char* data = req.bodyMem->data();
            while (sent < bodyLen)
            {
                if (IsCancelled(req))
                    return VoidResult::Failure(CancelledError());
                DWORD chunk = static_cast<DWORD>(std::min<uint64_t>(kChunkSize, bodyLen - sent));
                DWORD written = 0;
                if (!::WinHttpWriteData(request.h, data + sent, chunk, &written))
                    return VoidResult::Failure(NetworkError("upload", ::GetLastError()));
                sent += written;
                if (req.progress && !req.progress(sent, bodyLen))
                    return VoidResult::Failure(CancelledError());
            }
        }
    }

    if (!::WinHttpReceiveResponse(request.h, nullptr))
        return VoidResult::Failure(NetworkError("receive response", ::GetLastError()));

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!::WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                               WINHTTP_NO_HEADER_INDEX))
        return VoidResult::Failure(NetworkError("read status", ::GetLastError()));
    resp.status = static_cast<int>(status);

    // Collect all response headers.
    DWORD rawSize = 0;
    ::WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                          WINHTTP_NO_OUTPUT_BUFFER, &rawSize, WINHTTP_NO_HEADER_INDEX);
    if (rawSize > 0)
    {
        std::wstring raw(rawSize / sizeof(wchar_t), L'\0');
        if (::WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                  raw.data(), &rawSize, WINHTTP_NO_HEADER_INDEX))
        {
            std::string utf8 = WideToUtf8(raw.c_str()); // stop at embedded NUL
            size_t pos = 0;
            while (pos < utf8.size())
            {
                size_t end = utf8.find("\r\n", pos);
                if (end == std::string::npos)
                    end = utf8.size();
                std::string line = utf8.substr(pos, end - pos);
                pos = end + 2;
                size_t colon = line.find(':');
                if (colon != std::string::npos)
                {
                    std::string name = ToLowerAscii(Trim(line.substr(0, colon)));
                    std::string value = Trim(line.substr(colon + 1));
                    resp.headers[name] = value;
                }
            }
        }
    }

    // Read the body. Errors always land in memory so S3 XML can be parsed.
    const bool toFile = !req.sinkFile.empty() && resp.status >= 200 && resp.status < 300;
    FileWriter sink;
    if (toFile && !sink.Open(req.sinkFile))
    {
        StorageError e;
        e.kind = ErrorKind::LocalIo;
        e.win32 = ::GetLastError();
        e.message = "Cannot create local file for download";
        return VoidResult::Failure(e);
    }

    uint64_t total = 0;
    {
        auto it = resp.headers.find("content-length");
        if (it != resp.headers.end())
            total = ::_strtoui64(it->second.c_str(), nullptr, 10);
    }

    std::vector<unsigned char> buf(kChunkSize);
    for (;;)
    {
        if (IsCancelled(req))
            return VoidResult::Failure(CancelledError());
        DWORD got = 0;
        if (!::WinHttpReadData(request.h, buf.data(), kChunkSize, &got))
            return VoidResult::Failure(NetworkError("download", ::GetLastError()));
        if (got == 0)
            break;
        resp.bodyBytes += got;
        if (toFile)
        {
            if (!sink.Write(buf.data(), got))
            {
                StorageError e;
                e.kind = ErrorKind::LocalIo;
                e.win32 = ::GetLastError();
                e.message = "Local file write failed during download";
                return VoidResult::Failure(e);
            }
            if (req.progress && !req.progress(resp.bodyBytes, total))
                return VoidResult::Failure(CancelledError());
        }
        else
        {
            if (resp.bodyBytes > kMaxMemoryBody)
            {
                StorageError e;
                e.kind = ErrorKind::Internal;
                e.message = "Response body too large for in-memory handling";
                return VoidResult::Failure(e);
            }
            resp.body.append(reinterpret_cast<char*>(buf.data()), got);
        }
    }

    if (toFile && !sink.Close())
    {
        StorageError e;
        e.kind = ErrorKind::LocalIo;
        e.win32 = ::GetLastError();
        e.message = "Closing downloaded file failed";
        return VoidResult::Failure(e);
    }

    return VoidResult::Success();
}

} // namespace npps3
