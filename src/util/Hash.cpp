// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Hash.h"
#include "StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace npps3 {
namespace {

// Algorithm provider handles are process-wide and cheap to keep open.
BCRYPT_ALG_HANDLE Sha256Provider()
{
    static BCRYPT_ALG_HANDLE h = [] {
        BCRYPT_ALG_HANDLE alg = nullptr;
        ::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        return alg;
    }();
    return h;
}

BCRYPT_ALG_HANDLE HmacSha256Provider()
{
    static BCRYPT_ALG_HANDLE h = [] {
        BCRYPT_ALG_HANDLE alg = nullptr;
        ::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
        return alg;
    }();
    return h;
}

class HashObject
{
public:
    HashObject(BCRYPT_ALG_HANDLE alg, const void* key, size_t keyLen)
    {
        ::BCryptCreateHash(alg, &m_hash, nullptr, 0,
                           static_cast<PUCHAR>(const_cast<void*>(key)), static_cast<ULONG>(keyLen), 0);
    }
    ~HashObject()
    {
        if (m_hash)
            ::BCryptDestroyHash(m_hash);
    }
    HashObject(const HashObject&) = delete;
    HashObject& operator=(const HashObject&) = delete;

    bool Update(const void* data, size_t len)
    {
        if (!m_hash)
            return false;
        return BCRYPT_SUCCESS(::BCryptHashData(m_hash,
            static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(len), 0));
    }

    bool Finish(Sha256Digest& out)
    {
        if (!m_hash)
            return false;
        return BCRYPT_SUCCESS(::BCryptFinishHash(m_hash, out.data(), static_cast<ULONG>(out.size()), 0));
    }

private:
    BCRYPT_HASH_HANDLE m_hash = nullptr;
};

} // namespace

Sha256Digest Sha256(const void* data, size_t len)
{
    Sha256Digest out{};
    HashObject h(Sha256Provider(), nullptr, 0);
    h.Update(data, len);
    h.Finish(out);
    return out;
}

Sha256Digest Sha256(std::string_view data)
{
    return Sha256(data.data(), data.size());
}

bool Sha256File(const std::wstring& path, Sha256Digest& out)
{
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    HashObject h(Sha256Provider(), nullptr, 0);
    std::vector<unsigned char> buf(64 * 1024);
    bool ok = true;
    for (;;)
    {
        DWORD read = 0;
        if (!::ReadFile(file, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr))
        {
            ok = false;
            break;
        }
        if (read == 0)
            break;
        if (!h.Update(buf.data(), read))
        {
            ok = false;
            break;
        }
    }
    ::CloseHandle(file);
    return ok && h.Finish(out);
}

Sha256Digest HmacSha256(const void* key, size_t keyLen, std::string_view data)
{
    Sha256Digest out{};
    HashObject h(HmacSha256Provider(), key, keyLen);
    h.Update(data.data(), data.size());
    h.Finish(out);
    return out;
}

std::string Sha256Hex(std::string_view data)
{
    Sha256Digest d = Sha256(data);
    return HexLower(d.data(), d.size());
}

} // namespace npps3
