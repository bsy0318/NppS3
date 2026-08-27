// NppS3 — host-test helper CLI.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small utility used by the host-level GUI verification scripts:
//   s3cli put <bucket> <key> <localfile>     upload a file
//   s3cli get <bucket> <key> <localfile>     download a file
//   s3cli del <bucket> <key>                 delete an object
//   s3cli head <bucket> <key>               print etag/size (exit 2 if absent)
//   s3cli cachepath <profileId> <bucket> <key> <cacheRoot>
//        print the plugin's deterministic cache path for the object
//   s3cli storecred <profileId>             store NPPS3_TEST secret in
//        Windows Credential Manager under the plugin's target name
// Credentials come from NPPS3_TEST_* env vars or a .env in the CWD.
// Secret values are never printed.

#include "cache/CacheManager.h"
#include "security/CredentialStore.h"
#include "storage/S3Client.h"
#include "util/Mime.h"
#include "util/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <string>

using namespace npps3;

namespace {

std::string GetEnv(const char* name)
{
    char buf[4096];
    size_t len = 0;
    if (getenv_s(&len, buf, sizeof(buf), name) == 0 && len > 1)
        return std::string(buf, len - 1);
    return {};
}

std::map<std::string, std::string> LoadDotEnv(const std::string& path)
{
    std::map<std::string, std::string> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
    {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#')
            continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = Trim(t.substr(0, eq));
        std::string value = Trim(t.substr(eq + 1));
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\'')))
            value = value.substr(1, value.size() - 2);
        out[key] = value;
    }
    return out;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
    {
        std::printf("usage: s3cli put|get|del|head|cachepath|storecred ...\n");
        return 64;
    }
    std::string op = WideToUtf8(argv[1]);

    auto dotenv = LoadDotEnv(".env");
    auto resolve = [&](const char* name) -> std::string {
        std::string v = GetEnv(name);
        if (!v.empty())
            return v;
        auto it = dotenv.find(name);
        return it != dotenv.end() ? it->second : std::string();
    };

    if (op == "cachepath")
    {
        if (argc != 6)
            return 64;
        CacheManager cache;
        cache.SetRoot(argv[5]);
        std::wstring p = cache.LocalPathFor(WideToUtf8(argv[2]), WideToUtf8(argv[3]),
                                            WideToUtf8(argv[4]));
        std::wprintf(L"%s\n", p.c_str());
        return 0;
    }

    if (op == "storecred")
    {
        if (argc != 3)
            return 64;
        std::string secret = resolve("NPPS3_TEST_SECRET_ACCESS_KEY");
        if (secret.empty())
        {
            std::printf("no secret available\n");
            return 1;
        }
        bool ok = CredentialStore::Save(CredentialStore::TargetForProfile(argv[2]), secret);
        ::SecureZeroMemory(secret.data(), secret.size());
        std::printf(ok ? "stored\n" : "store failed\n");
        return ok ? 0 : 1;
    }

    S3Config cfg;
    cfg.endpoint = resolve("NPPS3_TEST_ENDPOINT");
    cfg.region = "auto";
    cfg.accessKeyId = resolve("NPPS3_TEST_ACCESS_KEY_ID");
    cfg.secretAccessKey = resolve("NPPS3_TEST_SECRET_ACCESS_KEY");
    if (cfg.endpoint.empty() || cfg.accessKeyId.empty() || cfg.secretAccessKey.empty())
    {
        std::printf("credentials unavailable\n");
        return 77;
    }
    S3Client client(cfg);

    if (op == "put" && argc == 5)
    {
        std::string key = WideToUtf8(argv[3]);
        auto r = client.PutObject(WideToUtf8(argv[2]), key, argv[4],
                                  MimeTypeForKey(key), nullptr);
        std::printf(r.ok ? "ok %s\n" : "FAIL %s\n",
                    r.ok ? r.value.etag.c_str() : r.error.Describe().c_str());
        return r.ok ? 0 : 1;
    }
    if (op == "get" && argc == 5)
    {
        auto r = client.DownloadObject(WideToUtf8(argv[2]), WideToUtf8(argv[3]),
                                       argv[4], nullptr);
        std::printf(r.ok ? "ok %s\n" : "FAIL %s\n",
                    r.ok ? r.value.etag.c_str() : r.error.Describe().c_str());
        return r.ok ? 0 : 1;
    }
    if (op == "del" && argc == 4)
    {
        VoidResult r = client.DeleteObject(WideToUtf8(argv[2]), WideToUtf8(argv[3]));
        std::printf(r.ok ? "ok\n" : "FAIL %s\n", r.ok ? "" : r.error.Describe().c_str());
        return r.ok ? 0 : 1;
    }
    if (op == "list" && argc == 4)
    {
        std::string bucket = WideToUtf8(argv[2]);
        std::string prefix = WideToUtf8(argv[3]);
        std::string token;
        do
        {
            auto r = client.ListObjects(bucket, prefix, "", token, 1000);
            if (!r.ok)
            {
                std::printf("FAIL %s\n", r.error.Describe().c_str());
                return 1;
            }
            for (const auto& o : r.value.objects)
                std::printf("%s\n", o.key.c_str());
            token = r.value.isTruncated ? r.value.nextContinuationToken : std::string();
        } while (!token.empty());
        return 0;
    }
    if (op == "head" && argc == 4)
    {
        auto r = client.HeadObject(WideToUtf8(argv[2]), WideToUtf8(argv[3]));
        if (r.ok)
        {
            std::printf("ok etag=%s size=%llu\n", r.value.etag.c_str(),
                        static_cast<unsigned long long>(r.value.size));
            return 0;
        }
        std::printf("FAIL %s\n", r.error.Describe().c_str());
        return r.error.kind == ErrorKind::NoSuchKey ? 2 : 1;
    }

    std::printf("bad arguments\n");
    return 64;
}
