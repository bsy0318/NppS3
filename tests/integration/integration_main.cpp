// NppS3 — real Cloudflare R2 integration tests.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Credentials come from NPPS3_TEST_* environment variables, or from a local
// .env file (first argument optionally overrides its path). Secret values are
// never printed; failures show only masked identifiers and error classes.
//
// All objects are created under npps3-integration-test/<run-id>/ and cleaned
// up afterwards. Nothing outside that namespace is touched.

#include "cache/CacheManager.h"
#include "documents/RemoteDocumentManager.h"
#include "storage/S3Client.h"
#include "transfer/TransferManager.h"
#include "util/Mime.h"
#include "util/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using namespace npps3;

namespace {

int g_passed = 0;
int g_failed = 0;
std::vector<std::string> g_failures;

void Report(const std::string& name, bool ok, const std::string& detail = "")
{
    if (ok)
    {
        ++g_passed;
        std::printf("[PASS] %s\n", name.c_str());
    }
    else
    {
        ++g_failed;
        g_failures.push_back(name + (detail.empty() ? "" : " — " + detail));
        std::printf("[FAIL] %s %s\n", name.c_str(), detail.c_str());
    }
}

std::string GetEnv(const char* name)
{
    char buf[4096];
    size_t len = 0;
    if (getenv_s(&len, buf, sizeof(buf), name) == 0 && len > 1)
        return std::string(buf, len - 1);
    return {};
}

// Minimal .env loader. Values never leave this process.
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
        if (!key.empty())
            out[key] = value;
    }
    return out;
}

std::string RunId()
{
    SYSTEMTIME st{};
    ::GetSystemTime(&st);
    char buf[64];
    ::_snprintf_s(buf, _TRUNCATE, "%04u%02u%02u-%02u%02u%02u-%lu",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                  ::GetCurrentProcessId());
    return buf;
}

std::wstring TempDir()
{
    wchar_t buf[MAX_PATH];
    ::GetTempPathW(MAX_PATH, buf);
    std::wstring dir = std::wstring(buf) + L"npps3-itest-" + std::to_wstring(::GetCurrentProcessId());
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

bool WriteLocalFile(const std::wstring& path, const std::string& content)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return f.good();
}

std::string ReadLocalFile(const std::wstring& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Collects transfer events for the async pipeline test.
class EventCollector : public ITransferObserver
{
public:
    void OnTransferEvent(const TransferEvent& ev) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ev.type == TransferEvent::Type::Progress)
            ++m_progressCount;
        if (ev.type == TransferEvent::Type::Finished)
            m_finished[ev.id] = ev;
    }

    bool WaitFinished(unsigned long long id, TransferEvent& out, int timeoutMs = 60000)
    {
        for (int waited = 0; waited < timeoutMs; waited += 50)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_finished.find(id);
                if (it != m_finished.end())
                {
                    out = it->second;
                    return true;
                }
            }
            ::Sleep(50);
        }
        return false;
    }

    int ProgressCount()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_progressCount;
    }

private:
    std::mutex m_mutex;
    std::map<unsigned long long, TransferEvent> m_finished;
    int m_progressCount = 0;
};

} // namespace

int main(int argc, char** argv)
{
    // --- credential resolution: process env first, then .env -----------------
    std::string envPath = argc > 1 ? argv[1] : ".env";
    auto dotenv = LoadDotEnv(envPath);
    auto resolve = [&](const char* name) -> std::string {
        std::string v = GetEnv(name);
        if (!v.empty())
            return v;
        auto it = dotenv.find(name);
        return it != dotenv.end() ? it->second : std::string();
    };

    const std::string endpoint = resolve("NPPS3_TEST_ENDPOINT");
    const std::string accessKey = resolve("NPPS3_TEST_ACCESS_KEY_ID");
    const std::string secretKey = resolve("NPPS3_TEST_SECRET_ACCESS_KEY");
    const std::string bucket = resolve("NPPS3_TEST_BUCKET");

    if (endpoint.empty() || accessKey.empty() || secretKey.empty() || bucket.empty())
    {
        std::printf("SKIP: NPPS3_TEST_* credentials not available (env or %s)\n", envPath.c_str());
        return 77; // conventional "skipped" exit code
    }

    std::printf("NppS3 R2 integration tests\n");
    std::printf("Access key: %s  Bucket: %s\n",
                MaskSensitive(accessKey).c_str(), MaskSensitive(bucket).c_str());

    S3Config cfg;
    cfg.endpoint = endpoint;
    cfg.region = "auto";
    cfg.accessKeyId = accessKey;
    cfg.secretAccessKey = secretKey;
    cfg.pathStyle = true;

    S3Client client(cfg);

    const std::string runPrefix = "npps3-integration-test/" + RunId() + "/";
    std::printf("Run prefix: %s\n\n", runPrefix.c_str());
    std::vector<std::string> createdKeys; // tracked for cleanup

    const std::wstring tmpDir = TempDir();

    // --- 1. Authentication ---------------------------------------------------
    {
        VoidResult r = client.TestConnection(bucket);
        Report("auth: connect + ListObjectsV2", r.ok, r.ok ? "" : r.error.Describe());
        if (!r.ok)
        {
            std::printf("\nAborting: cannot authenticate.\n");
            return 1;
        }
    }

    // --- 2. ListBuckets (permission-dependent) -------------------------------
    {
        auto r = client.ListBuckets();
        if (r.ok)
        {
            bool found = false;
            for (const auto& b : r.value)
                if (b.name == bucket)
                    found = true;
            Report("ListBuckets contains test bucket", found,
                   found ? "" : "test bucket not in listing");
        }
        else
        {
            // Bucket-scoped R2 tokens cannot list buckets; that is not a failure.
            std::printf("[INFO] ListBuckets not permitted for this token (%s)\n",
                        r.error.s3Code.c_str());
        }
    }

    // --- 3. Upload (PutObject) + verify via Head ----------------------------
    const std::string content1 = "NppS3 integration test v1\nline2\n";
    const std::string key1 = runPrefix + "basic/upload.txt";
    {
        std::wstring local = tmpDir + L"\\upload.txt";
        bool wrote = WriteLocalFile(local, content1);
        auto r = client.PutObject(bucket, key1, local, MimeTypeForKey(key1), nullptr);
        if (r.ok)
            createdKeys.push_back(key1);
        Report("PutObject", wrote && r.ok, r.ok ? "" : r.error.Describe());

        auto h = client.HeadObject(bucket, key1);
        Report("HeadObject size/content-type", h.ok && h.value.size == content1.size() &&
                   h.value.contentType == "text/plain",
               h.ok ? "" : h.error.Describe());
    }

    // --- 4. Download (GetObject) + content verification ----------------------
    {
        std::wstring local = tmpDir + L"\\download.txt";
        auto r = client.DownloadObject(bucket, key1, local, nullptr);
        bool same = r.ok && ReadLocalFile(local) == content1;
        Report("GetObject content matches", same, r.ok ? "" : r.error.Describe());
        Report("GetObject returns ETag", r.ok && !r.value.etag.empty());
    }

    // --- 5. Prefix/delimiter listing ----------------------------------------
    {
        for (const char* k : {"tree/a/1.txt", "tree/a/b/2.txt", "tree/c.txt"})
        {
            std::string key = runPrefix + k;
            auto r = client.PutObjectBytes(bucket, key, "x", "text/plain");
            if (r.ok)
                createdKeys.push_back(key);
        }
        auto r = client.ListObjects(bucket, runPrefix + "tree/", "/", "", 1000);
        bool ok = r.ok;
        bool prefixOk = false, objOk = false;
        if (ok)
        {
            for (const auto& p : r.value.commonPrefixes)
                if (p == runPrefix + "tree/a/")
                    prefixOk = true;
            for (const auto& o : r.value.objects)
                if (o.key == runPrefix + "tree/c.txt")
                    objOk = true;
        }
        Report("List with delimiter: folder-like prefixes", ok && prefixOk && objOk,
               ok ? "" : r.error.Describe());
    }

    // --- 6. Pagination -------------------------------------------------------
    {
        std::vector<std::string> pageKeys;
        for (int i = 0; i < 12; ++i)
        {
            char name[32];
            ::_snprintf_s(name, _TRUNCATE, "page/obj-%02d.txt", i);
            std::string key = runPrefix + name;
            auto r = client.PutObjectBytes(bucket, key, "p", "text/plain");
            if (r.ok)
            {
                pageKeys.push_back(key);
                createdKeys.push_back(key);
            }
        }

        std::vector<std::string> seen;
        std::string token;
        int pages = 0;
        bool ok = true;
        do
        {
            auto r = client.ListObjects(bucket, runPrefix + "page/", "", token, 5);
            if (!r.ok)
            {
                ok = false;
                break;
            }
            ++pages;
            for (const auto& o : r.value.objects)
                seen.push_back(o.key);
            token = r.value.isTruncated ? r.value.nextContinuationToken : "";
        } while (!token.empty() && pages < 10);

        Report("Pagination (12 keys, max-keys=5)",
               ok && pages >= 3 && seen.size() == pageKeys.size());
    }

    // --- 7. Modify / re-upload ----------------------------------------------
    const std::string content2 = "NppS3 integration test v2 — modified\n";
    {
        std::wstring local = tmpDir + L"\\upload.txt";
        WriteLocalFile(local, content2);
        auto r = client.PutObject(bucket, key1, local, "text/plain", nullptr);
        std::wstring local2 = tmpDir + L"\\download2.txt";
        auto g = client.DownloadObject(bucket, key1, local2, nullptr);
        Report("Modify + re-upload roundtrip",
               r.ok && g.ok && ReadLocalFile(local2) == content2,
               r.ok ? (g.ok ? "" : g.error.Describe()) : r.error.Describe());
    }

    // --- 8. Rename / move (copy → verify → delete source) --------------------
    {
        const std::string oldKey = runPrefix + "rename/old.txt";
        const std::string newKey = runPrefix + "rename/new.txt";
        auto p = client.PutObjectBytes(bucket, oldKey, "rename-me", "text/plain");
        if (p.ok)
            createdKeys.push_back(oldKey);
        auto c = client.CopyObject(bucket, oldKey, bucket, newKey);
        if (c.ok)
            createdKeys.push_back(newKey);
        auto verifyNew = client.HeadObject(bucket, newKey);
        VoidResult del = verifyNew.ok ? client.DeleteObject(bucket, oldKey)
                                      : VoidResult::Failure({});
        auto verifyOld = client.HeadObject(bucket, oldKey);
        bool oldGone = !verifyOld.ok && verifyOld.error.kind == ErrorKind::NoSuchKey;
        Report("Rename: copy, verify, delete source, verify gone",
               p.ok && c.ok && verifyNew.ok && del.ok && oldGone);
    }

    // --- 9. Unicode keys -----------------------------------------------------
    {
        const std::string uniKeys[] = {
            runPrefix + "\xed\x95\x9c\xea\xb8\x80/\xed\x85\x8c\xec\x8a\xa4\xed\x8a\xb8.json", // 한글/테스트.json
            runPrefix + "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e/test.txt",                       // 日本語/test.txt
            runPrefix + "special chars/#test?.txt",
        };
        for (const std::string& key : uniKeys)
        {
            const std::string body = "unicode:" + key;
            auto p = client.PutObjectBytes(bucket, key, body, MimeTypeForKey(key));
            if (p.ok)
                createdKeys.push_back(key);

            bool listed = false;
            auto l = client.ListObjects(bucket, runPrefix, "", "", 1000);
            if (l.ok)
                for (const auto& o : l.value.objects)
                    if (o.key == key)
                        listed = true;

            std::wstring local = tmpDir + L"\\uni-" + std::to_wstring(createdKeys.size()) + L".bin";
            auto g = client.DownloadObject(bucket, key, local, nullptr);
            bool same = g.ok && ReadLocalFile(local) == body;

            VoidResult d = client.DeleteObject(bucket, key);
            auto gone = client.HeadObject(bucket, key);
            bool isGone = !gone.ok && gone.error.kind == ErrorKind::NoSuchKey;

            Report("Unicode key up/list/down/delete: " + MaskSensitive(key),
                   p.ok && listed && same && d.ok && isGone);
        }
    }

    // --- 10. Delete + verify -------------------------------------------------
    {
        const std::string key = runPrefix + "delete/me.txt";
        auto p = client.PutObjectBytes(bucket, key, "bye", "text/plain");
        VoidResult d = client.DeleteObject(bucket, key);
        auto h = client.HeadObject(bucket, key);
        Report("DeleteObject verify", p.ok && d.ok && !h.ok && h.error.kind == ErrorKind::NoSuchKey);
    }

    // --- 11. Async TransferManager pipeline ----------------------------------
    {
        EventCollector events;
        TransferManager mgr(&events);
        mgr.Start();

        // Large-ish payload so progress events fire.
        std::string big(512 * 1024, 'A');
        std::wstring localBig = tmpDir + L"\\big.bin";
        WriteLocalFile(localBig, big);
        const std::string bigKey = runPrefix + "async/big.bin";

        TransferRequest up;
        up.op = TransferOp::Upload;
        up.s3 = cfg;
        up.bucket = bucket;
        up.key = bigKey;
        up.localPath = localBig;
        up.contentType = "application/octet-stream";
        unsigned long long upId = mgr.Enqueue(up);

        TransferEvent upDone;
        bool upFinished = events.WaitFinished(upId, upDone);
        if (upFinished && upDone.state == TransferState::Completed)
            createdKeys.push_back(bigKey);
        Report("TransferManager async upload",
               upFinished && upDone.state == TransferState::Completed);

        TransferRequest down;
        down.op = TransferOp::Download;
        down.s3 = cfg;
        down.bucket = bucket;
        down.key = bigKey;
        down.localPath = tmpDir + L"\\big-down.bin";
        unsigned long long downId = mgr.Enqueue(down);

        TransferEvent downDone;
        bool downFinished = events.WaitFinished(downId, downDone);
        Report("TransferManager async download + content",
               downFinished && downDone.state == TransferState::Completed &&
                   ReadLocalFile(down.localPath) == big);
        Report("TransferManager emitted progress events", events.ProgressCount() > 0);

        mgr.Shutdown();
    }

    // --- 12. Remote-edit pipeline (save → mapped upload), no GUI -------------
    {
        CacheManager cache;
        cache.SetRoot(tmpDir + L"\\cache");
        RemoteDocumentManager docs;

        const std::string key = runPrefix + "remote-edit/config.json";
        const std::string original = "{\"version\":1}";
        auto p = client.PutObjectBytes(bucket, key, original, "application/json");
        if (p.ok)
            createdKeys.push_back(key);

        // Open: download into managed cache and register the mapping.
        std::wstring local = cache.LocalPathFor("itest-profile", bucket, key);
        CacheManager::EnsureParentDirs(local);
        auto g = client.DownloadObject(bucket, key, local, nullptr);

        RemoteDocument doc;
        doc.profileId = "itest-profile";
        doc.bucket = bucket;
        doc.key = key;
        doc.localPath = local;
        doc.etag = g.ok ? g.value.etag : "";
        doc.autoUpload = true;
        docs.Register(doc);

        // Edit + save: what the NPPN_FILESAVED handler does.
        const std::string modified = "{\"version\":2,\"edited\":true}";
        WriteLocalFile(local, modified);
        const RemoteDocument* found = docs.FindByLocalPath(local);
        bool uploaded = false;
        if (found && found->autoUpload)
        {
            auto u = client.PutObject(found->bucket, found->key, found->localPath,
                                      "application/json", nullptr);
            uploaded = u.ok;
            if (u.ok)
                docs.UpdateRemoteState(local, u.value.etag, "");
        }

        // Verify remote now holds the modification.
        std::wstring verify = tmpDir + L"\\verify.json";
        auto v = client.DownloadObject(bucket, key, verify, nullptr);
        Report("Remote-edit pipeline: open→edit→save→auto-upload→verify",
               p.ok && g.ok && found != nullptr && uploaded && v.ok &&
                   ReadLocalFile(verify) == modified);

        // An unrelated file must not map to any remote document.
        Report("Remote-edit pipeline: unrelated file not mapped",
               docs.FindByLocalPath(tmpDir + L"\\upload.txt") == nullptr);
    }

    // --- 13. Failure handling ------------------------------------------------
    {
        S3Config bad = cfg;
        bad.secretAccessKey = "invalid-secret-invalid-secret-invalid-12";
        S3Client badClient(bad);
        auto r = badClient.ListObjects(bucket, "", "", "", 1);
        Report("Invalid secret is rejected as credential error",
               !r.ok && (r.error.kind == ErrorKind::InvalidCredentials ||
                         r.error.kind == ErrorKind::AccessDenied));
    }
    {
        auto r = client.ListObjects("npps3-no-such-bucket-a8f2k1", "", "", "", 1);
        Report("Nonexistent bucket surfaces an error",
               !r.ok && (r.error.kind == ErrorKind::NoSuchBucket ||
                         r.error.kind == ErrorKind::AccessDenied ||
                         r.error.kind == ErrorKind::NoSuchKey));
    }
    {
        S3Config bad = cfg;
        bad.endpoint = "https://nonexistent-host-npps3.invalid";
        bad.maxRetries = 1;
        S3Client badClient(bad);
        auto r = badClient.ListObjects(bucket, "", "", "", 1);
        Report("Invalid endpoint surfaces a network error",
               !r.ok && r.error.kind == ErrorKind::Network);
    }
    {
        auto h = client.HeadObject(bucket, runPrefix + "definitely-not-there.txt");
        Report("Missing object -> NoSuchKey",
               !h.ok && h.error.kind == ErrorKind::NoSuchKey);
    }

    // --- Cleanup -------------------------------------------------------------
    {
        int cleaned = 0;
        for (const std::string& key : createdKeys)
        {
            VoidResult d = client.DeleteObject(bucket, key);
            if (d.ok)
                ++cleaned;
        }
        // Catch anything the tracker missed inside the run namespace.
        auto l = client.ListObjects(bucket, runPrefix, "", "", 1000);
        if (l.ok)
            for (const auto& o : l.value.objects)
                if (client.DeleteObject(bucket, o.key).ok)
                    ++cleaned;
        auto post = client.ListObjects(bucket, runPrefix, "", "", 10);
        Report("Cleanup: run namespace empty", post.ok && post.value.objects.empty());
        std::printf("[INFO] Cleaned %d objects under run prefix\n", cleaned);
    }

    std::printf("\n=== %d passed, %d failed ===\n", g_passed, g_failed);
    for (const auto& f : g_failures)
        std::printf("  FAILED: %s\n", f.c_str());
    return g_failed == 0 ? 0 : 1;
}
