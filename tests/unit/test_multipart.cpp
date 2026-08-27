// NppS3 unit tests — multipart planning, XML, resume journal and resume logic.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest.h>

#include "storage/MultipartUpload.h"
#include "storage/S3Xml.h"
#include "storage/UploadJournal.h"

#include <algorithm>

using namespace npps3;

namespace {

constexpr uint64_t kMiB = 1024ull * 1024;
constexpr uint64_t kGiB = 1024ull * kMiB;

// Records every multipart call so resume decisions can be asserted without a
// network. Only the multipart surface is implemented; the rest traps.
class FakeStorage : public IObjectStorage
{
public:
    std::vector<MultipartPart> existingParts;
    bool listPartsFails = false;

    std::string createdUploadId = "upload-new";
    int createCalls = 0;
    int abortCalls = 0;
    std::vector<int> uploadedPartNumbers;
    std::vector<MultipartPart> completedWith;
    std::string completedUploadId;

    Outcome<std::string> CreateMultipartUpload(const std::string&, const std::string&,
                                               const std::string&) override
    {
        ++createCalls;
        return Outcome<std::string>::Success(createdUploadId);
    }

    Outcome<MultipartPart> UploadPart(const std::string&, const std::string&,
                                      const std::string&, int partNumber,
                                      const std::wstring&, uint64_t, uint64_t length,
                                      const ProgressFn& progress) override
    {
        uploadedPartNumbers.push_back(partNumber);
        if (progress)
            progress(length, length);
        MultipartPart p;
        p.partNumber = partNumber;
        p.size = length;
        p.etag = "etag-" + std::to_string(partNumber);
        return Outcome<MultipartPart>::Success(p);
    }

    Outcome<PutObjectResult> CompleteMultipartUpload(const std::string&, const std::string&,
                                                     const std::string& uploadId,
                                                     const std::vector<MultipartPart>& parts) override
    {
        completedUploadId = uploadId;
        completedWith = parts;
        PutObjectResult r;
        r.etag = "final-etag";
        return Outcome<PutObjectResult>::Success(r);
    }

    VoidResult AbortMultipartUpload(const std::string&, const std::string&,
                                    const std::string&) override
    {
        ++abortCalls;
        return VoidResult::Success();
    }

    Outcome<std::vector<MultipartPart>> ListParts(const std::string&, const std::string&,
                                                  const std::string&) override
    {
        if (listPartsFails)
        {
            StorageError e;
            e.kind = ErrorKind::NoSuchKey;
            e.s3Code = "NoSuchUpload";
            return Outcome<std::vector<MultipartPart>>::Failure(e);
        }
        return Outcome<std::vector<MultipartPart>>::Success(existingParts);
    }

    Outcome<std::vector<MultipartUploadInfo>> ListMultipartUploads(const std::string&,
                                                                   const std::string&) override
    {
        return Outcome<std::vector<MultipartUploadInfo>>::Success({});
    }

    // --- unused by these tests ---------------------------------------------
    Outcome<std::vector<BucketInfo>> ListBuckets() override { return Fail<std::vector<BucketInfo>>(); }
    Outcome<ListObjectsResult> ListObjects(const std::string&, const std::string&,
                                           const std::string&, const std::string&, int) override
    { return Fail<ListObjectsResult>(); }
    Outcome<ObjectMetadata> HeadObject(const std::string&, const std::string&) override
    { return Fail<ObjectMetadata>(); }
    Outcome<ObjectMetadata> DownloadObject(const std::string&, const std::string&,
                                           const std::wstring&, const ProgressFn&) override
    { return Fail<ObjectMetadata>(); }
    Outcome<PutObjectResult> PutObject(const std::string&, const std::string&,
                                       const std::wstring&, const std::string&,
                                       const ProgressFn&) override
    { return Fail<PutObjectResult>(); }
    Outcome<PutObjectResult> PutObjectBytes(const std::string&, const std::string&,
                                            const std::string&, const std::string&) override
    { return Fail<PutObjectResult>(); }
    VoidResult DeleteObject(const std::string&, const std::string&) override
    { return VoidResult::Failure({}); }
    Outcome<PutObjectResult> CopyObject(const std::string&, const std::string&,
                                        const std::string&, const std::string&) override
    { return Fail<PutObjectResult>(); }
    VoidResult TestConnection(const std::string&) override { return VoidResult::Failure({}); }

private:
    template <typename T>
    static Outcome<T> Fail()
    {
        StorageError e;
        e.kind = ErrorKind::Internal;
        e.message = "not implemented in test double";
        return Outcome<T>::Failure(e);
    }
};

// In-memory journal so resume can be driven without touching the disk.
class MemoryJournal final : public IUploadJournal
{
public:
    std::vector<UploadJournalEntry> entries;

    std::optional<UploadJournalEntry> Find(const std::string& scope, const std::string& bucket,
                                           const std::string& key) const override
    {
        for (const auto& e : entries)
            if (e.scope == scope && e.bucket == bucket && e.key == key)
                return e;
        return std::nullopt;
    }
    void Begin(const UploadJournalEntry& entry) override { entries.push_back(entry); }
    void RecordPart(const std::string& scope, const std::string& bucket, const std::string& key,
                    const MultipartPart& part) override
    {
        for (auto& e : entries)
            if (e.scope == scope && e.bucket == bucket && e.key == key)
                e.parts.push_back(part);
    }
    void Remove(const std::string& scope, const std::string& bucket,
                const std::string& key) override
    {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const UploadJournalEntry& e) {
                                         return e.scope == scope && e.bucket == bucket &&
                                                e.key == key;
                                     }),
                      entries.end());
    }
};

} // namespace

TEST_CASE("PlanMultipartUpload: uniform parts within the S3 limits")
{
    SUBCASE("desired part size is honoured when it fits")
    {
        auto r = PlanMultipartUpload(100 * kMiB, 16 * kMiB);
        REQUIRE(r.ok);
        CHECK(r.value.partSize == 16 * kMiB);
        CHECK(r.value.partCount == 7); // 6 full + remainder
    }

    SUBCASE("part size is never below the 5 MiB S3 minimum")
    {
        auto r = PlanMultipartUpload(50 * kMiB, 1 * kMiB);
        REQUIRE(r.ok);
        CHECK(r.value.partSize == kMinPartSize);
    }

    SUBCASE("part size grows so the part count stays within the cap")
    {
        // 800 GiB at 16 MiB per part would need 51200 parts.
        auto r = PlanMultipartUpload(800 * kGiB, 16 * kMiB);
        REQUIRE(r.ok);
        CHECK(r.value.partCount <= static_cast<uint64_t>(kMaxParts));
        CHECK(r.value.partSize % kMiB == 0);
        CHECK(r.value.partSize * r.value.partCount >= 800 * kGiB);
    }

    SUBCASE("objects far past 4 GiB are planned, not rejected")
    {
        auto r = PlanMultipartUpload(6 * kGiB, 16 * kMiB);
        REQUIRE(r.ok);
        CHECK(r.value.partCount == 384);
        CHECK(PartOffset(r.value, 384) == 383ull * 16 * kMiB);
        CHECK(PartLength(r.value, 6 * kGiB, 384) == 16 * kMiB);
        // Every part stays under the 4 GiB single-request ceiling.
        CHECK(r.value.partSize < 4 * kGiB);
    }

    SUBCASE("last part carries the remainder")
    {
        auto r = PlanMultipartUpload(5 * kMiB + 7, 5 * kMiB);
        REQUIRE(r.ok);
        CHECK(r.value.partCount == 2);
        CHECK(PartLength(r.value, 5 * kMiB + 7, 1) == 5 * kMiB);
        CHECK(PartLength(r.value, 5 * kMiB + 7, 2) == 7);
    }

    SUBCASE("empty objects do not use multipart")
    {
        CHECK_FALSE(PlanMultipartUpload(0, 16 * kMiB).ok);
    }

    SUBCASE("beyond max parts times max part size the object is rejected")
    {
        auto r = PlanMultipartUpload(60000ull * kGiB, 16 * kMiB);
        CHECK_FALSE(r.ok);
    }
}

TEST_CASE("ParseInitiateMultipartUpload")
{
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<InitiateMultipartUploadResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Bucket>test</Bucket><Key>big.bin</Key>
  <UploadId>2~abcDEF-123_456</UploadId>
</InitiateMultipartUploadResult>)";
    auto r = ParseInitiateMultipartUpload(xml);
    REQUIRE(r.has_value());
    CHECK(*r == "2~abcDEF-123_456");
    CHECK_FALSE(ParseInitiateMultipartUpload("<Error><Code>AccessDenied</Code></Error>").has_value());
}

TEST_CASE("ParseListParts: sizes and etags for resume")
{
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<ListPartsResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Bucket>test</Bucket><Key>big.bin</Key><UploadId>u1</UploadId>
  <IsTruncated>true</IsTruncated>
  <NextPartNumberMarker>2</NextPartNumberMarker>
  <Part><PartNumber>1</PartNumber><ETag>&quot;aaa&quot;</ETag><Size>8388608</Size></Part>
  <Part><PartNumber>2</PartNumber><ETag>&quot;bbb&quot;</ETag><Size>8388608</Size></Part>
</ListPartsResult>)";
    auto r = ParseListParts(xml);
    REQUIRE(r.has_value());
    CHECK(r->isTruncated);
    CHECK(r->nextPartNumberMarker == 2);
    REQUIRE(r->parts.size() == 2);
    CHECK(r->parts[0].partNumber == 1);
    CHECK(r->parts[0].etag == "aaa"); // quotes stripped
    CHECK(r->parts[1].size == 8 * kMiB);
}

TEST_CASE("ParseListMultipartUploads")
{
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<ListMultipartUploadsResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Bucket>test</Bucket>
  <IsTruncated>false</IsTruncated>
  <Upload><Key>a/big.bin</Key><UploadId>u1</UploadId>
    <Initiated>2026-08-27T10:00:00.000Z</Initiated></Upload>
  <Upload><Key>b/other.bin</Key><UploadId>u2</UploadId></Upload>
</ListMultipartUploadsResult>)";
    auto r = ParseListMultipartUploads(xml);
    REQUIRE(r.has_value());
    REQUIRE(r->uploads.size() == 2);
    CHECK(r->uploads[0].key == "a/big.bin");
    CHECK(r->uploads[0].uploadId == "u1");
    CHECK(r->uploads[1].key == "b/other.bin");
    CHECK_FALSE(r->isTruncated);
}

TEST_CASE("CompleteMultipartUpload body and response")
{
    std::vector<MultipartPart> parts;
    parts.push_back({1, 5 * kMiB, "aaa"});
    parts.push_back({2, 12, "bbb"});
    const std::string body = BuildCompleteMultipartUploadXml(parts);
    CHECK(body.find("<PartNumber>1</PartNumber>") != std::string::npos);
    CHECK(body.find("&quot;aaa&quot;") != std::string::npos);
    CHECK(body.find("<PartNumber>2</PartNumber>") != std::string::npos);

    const std::string ok = R"(<CompleteMultipartUploadResult>
  <ETag>&quot;3858f62230ac3c915f300c664312c11f-2&quot;</ETag>
</CompleteMultipartUploadResult>)";
    auto etag = ParseCompleteMultipartUpload(ok);
    REQUIRE(etag.has_value());
    CHECK(*etag == "3858f62230ac3c915f300c664312c11f-2");
}

TEST_CASE("FileUploadJournal: round-trips entries through XML")
{
    FileUploadJournal journal;
    UploadJournalEntry entry;
    entry.scope = "scope1";
    entry.bucket = "bucket";
    entry.key = "\xed\x95\x9c\xea\xb8\x80/big.bin"; // 한글/big.bin
    entry.uploadId = "u1";
    entry.localPath = L"C:\\cache\\big.bin";
    entry.fileSize = 40 * kMiB;
    entry.fileTime = 133000000000000000ull;
    entry.partSize = 16 * kMiB;
    journal.Begin(entry);
    journal.RecordPart("scope1", "bucket", entry.key, {1, 16 * kMiB, "aaa"});
    journal.RecordPart("scope1", "bucket", entry.key, {2, 16 * kMiB, "bbb"});

    const std::string xml = journal.SerializeToXml();

    FileUploadJournal restored;
    REQUIRE(restored.DeserializeFromXml(xml));
    auto found = restored.Find("scope1", "bucket", entry.key);
    REQUIRE(found.has_value());
    CHECK(found->uploadId == "u1");
    CHECK(found->localPath == L"C:\\cache\\big.bin");
    CHECK(found->fileSize == 40 * kMiB);
    CHECK(found->fileTime == 133000000000000000ull);
    REQUIRE(found->parts.size() == 2);
    CHECK(found->parts[1].etag == "bbb");

    CHECK_FALSE(restored.Find("other-scope", "bucket", entry.key).has_value());
    restored.Remove("scope1", "bucket", entry.key);
    CHECK_FALSE(restored.Find("scope1", "bucket", entry.key).has_value());
}

TEST_CASE("MultipartUploader: fresh upload sends every part")
{
    FakeStorage storage;
    MemoryJournal journal;
    auto plan = PlanMultipartUpload(20 * kMiB, 5 * kMiB);
    REQUIRE(plan.ok);

    MultipartUploader uploader(storage, &journal, "scope1");
    uint64_t lastReported = 0;
    auto r = uploader.Upload("bucket", "big.bin", L"C:\\tmp\\big.bin", "application/octet-stream",
                             20 * kMiB, 1234, plan.value,
                             [&](uint64_t done, uint64_t) { lastReported = done; return true; });

    REQUIRE(r.ok);
    CHECK(r.value.etag == "final-etag");
    CHECK(storage.createCalls == 1);
    CHECK(storage.uploadedPartNumbers == std::vector<int>{1, 2, 3, 4});
    CHECK(storage.completedWith.size() == 4);
    CHECK(lastReported == 20 * kMiB);
    CHECK_FALSE(uploader.Resumed());
    // The journal entry is cleared once the object exists.
    CHECK(journal.entries.empty());
}

TEST_CASE("MultipartUploader: resumes an interrupted upload")
{
    FakeStorage storage;
    MemoryJournal journal;
    auto plan = PlanMultipartUpload(20 * kMiB, 5 * kMiB);
    REQUIRE(plan.ok);

    UploadJournalEntry entry;
    entry.scope = "scope1";
    entry.bucket = "bucket";
    entry.key = "big.bin";
    entry.uploadId = "u-interrupted";
    entry.fileSize = 20 * kMiB;
    entry.fileTime = 1234;
    entry.partSize = 5 * kMiB;
    entry.parts = {{1, 5 * kMiB, "etag-1"}, {2, 5 * kMiB, "etag-2"}};
    journal.entries.push_back(entry);
    storage.existingParts = entry.parts;

    MultipartUploader uploader(storage, &journal, "scope1");
    uint64_t firstReported = 0;
    bool sawFirst = false;
    auto r = uploader.Upload("bucket", "big.bin", L"C:\\tmp\\big.bin", "application/octet-stream",
                             20 * kMiB, 1234, plan.value,
                             [&](uint64_t done, uint64_t) {
                                 if (!sawFirst) { firstReported = done; sawFirst = true; }
                                 return true;
                             });

    REQUIRE(r.ok);
    CHECK(storage.createCalls == 0); // reused the existing upload id
    CHECK(storage.completedUploadId == "u-interrupted");
    CHECK(storage.uploadedPartNumbers == std::vector<int>{3, 4});
    CHECK(uploader.Resumed());
    CHECK(uploader.ResumedBytes() == 10 * kMiB);
    // Progress starts from what was already stored, not from zero.
    CHECK(firstReported == 5 * kMiB);
    REQUIRE(storage.completedWith.size() == 4);
    CHECK(storage.completedWith[0].etag == "etag-1");
}

TEST_CASE("MultipartUploader: a changed local file aborts the stale upload")
{
    FakeStorage storage;
    MemoryJournal journal;
    auto plan = PlanMultipartUpload(20 * kMiB, 5 * kMiB);
    REQUIRE(plan.ok);

    UploadJournalEntry entry;
    entry.scope = "scope1";
    entry.bucket = "bucket";
    entry.key = "big.bin";
    entry.uploadId = "u-stale";
    entry.fileSize = 20 * kMiB;
    entry.fileTime = 1111; // file was modified since
    entry.partSize = 5 * kMiB;
    entry.parts = {{1, 5 * kMiB, "etag-1"}};
    journal.entries.push_back(entry);
    storage.existingParts = entry.parts;

    MultipartUploader uploader(storage, &journal, "scope1");
    auto r = uploader.Upload("bucket", "big.bin", L"C:\\tmp\\big.bin", "application/octet-stream",
                             20 * kMiB, 2222, plan.value, nullptr);

    REQUIRE(r.ok);
    CHECK(storage.abortCalls == 1);
    CHECK(storage.createCalls == 1);
    CHECK(storage.uploadedPartNumbers == std::vector<int>{1, 2, 3, 4});
    CHECK_FALSE(uploader.Resumed());
}

TEST_CASE("MultipartUploader: journalled parts the service lost are re-sent")
{
    FakeStorage storage;
    MemoryJournal journal;
    auto plan = PlanMultipartUpload(20 * kMiB, 5 * kMiB);
    REQUIRE(plan.ok);

    UploadJournalEntry entry;
    entry.scope = "scope1";
    entry.bucket = "bucket";
    entry.key = "big.bin";
    entry.uploadId = "u1";
    entry.fileSize = 20 * kMiB;
    entry.fileTime = 1234;
    entry.partSize = 5 * kMiB;
    entry.parts = {{1, 5 * kMiB, "etag-1"}, {2, 5 * kMiB, "etag-2"}};
    journal.entries.push_back(entry);
    // The service only kept part 1, and with a different etag for part 2.
    storage.existingParts = {{1, 5 * kMiB, "etag-1"}, {2, 5 * kMiB, "different"}};

    MultipartUploader uploader(storage, &journal, "scope1");
    auto r = uploader.Upload("bucket", "big.bin", L"C:\\tmp\\big.bin", "application/octet-stream",
                             20 * kMiB, 1234, plan.value, nullptr);

    REQUIRE(r.ok);
    CHECK(storage.uploadedPartNumbers == std::vector<int>{2, 3, 4});
    CHECK(uploader.ResumedBytes() == 5 * kMiB);
}

TEST_CASE("MultipartUploader: an expired upload id restarts cleanly")
{
    FakeStorage storage;
    storage.listPartsFails = true;
    MemoryJournal journal;
    auto plan = PlanMultipartUpload(20 * kMiB, 5 * kMiB);
    REQUIRE(plan.ok);

    UploadJournalEntry entry;
    entry.scope = "scope1";
    entry.bucket = "bucket";
    entry.key = "big.bin";
    entry.uploadId = "u-gone";
    entry.fileSize = 20 * kMiB;
    entry.fileTime = 1234;
    entry.partSize = 5 * kMiB;
    entry.parts = {{1, 5 * kMiB, "etag-1"}};
    journal.entries.push_back(entry);

    MultipartUploader uploader(storage, &journal, "scope1");
    auto r = uploader.Upload("bucket", "big.bin", L"C:\\tmp\\big.bin", "application/octet-stream",
                             20 * kMiB, 1234, plan.value, nullptr);

    REQUIRE(r.ok);
    CHECK(storage.createCalls == 1);
    CHECK(storage.completedUploadId == "upload-new");
    CHECK(storage.uploadedPartNumbers == std::vector<int>{1, 2, 3, 4});
}

TEST_CASE("MultipartUploader: a failed part keeps the upload resumable")
{
    class FailingStorage final : public FakeStorage
    {
    public:
        Outcome<MultipartPart> UploadPart(const std::string& b, const std::string& k,
                                          const std::string& u, int partNumber,
                                          const std::wstring& p, uint64_t off, uint64_t len,
                                          const ProgressFn& prog) override
        {
            if (partNumber == 3)
            {
                StorageError e;
                e.kind = ErrorKind::Network;
                e.message = "connection reset";
                return Outcome<MultipartPart>::Failure(e);
            }
            return FakeStorage::UploadPart(b, k, u, partNumber, p, off, len, prog);
        }
    };

    FailingStorage storage;
    MemoryJournal journal;
    auto plan = PlanMultipartUpload(20 * kMiB, 5 * kMiB);
    REQUIRE(plan.ok);

    MultipartUploader uploader(storage, &journal, "scope1");
    auto r = uploader.Upload("bucket", "big.bin", L"C:\\tmp\\big.bin", "application/octet-stream",
                             20 * kMiB, 1234, plan.value, nullptr);

    CHECK_FALSE(r.ok);
    CHECK(r.error.kind == ErrorKind::Network);
    CHECK(storage.abortCalls == 0); // nothing is thrown away
    REQUIRE(journal.entries.size() == 1);
    CHECK(journal.entries[0].uploadId == "upload-new");
    CHECK(journal.entries[0].parts.size() == 2); // parts 1 and 2 survived
}
