// NppS3 unit tests — remote/local document mapping.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest.h>

#include "documents/RemoteDocumentManager.h"

using namespace npps3;

static RemoteDocument MakeDoc()
{
    RemoteDocument d;
    d.profileId = "profile-1";
    d.bucket = "bucket";
    d.key = "dir/\xed\x95\x9c\xea\xb8\x80.txt"; // dir/한글.txt
    d.localPath = L"C:\\Cache\\p1\\bucket\\abcd1234-\xd55c\xae00.txt";
    d.etag = "etag-1";
    d.lastModified = "Wed, 27 Aug 2026 10:00:00 GMT";
    d.autoUpload = true;
    return d;
}

TEST_CASE("Register and find by path is case-insensitive")
{
    RemoteDocumentManager mgr;
    mgr.Register(MakeDoc());

    const RemoteDocument* d = mgr.FindByLocalPath(L"C:\\CACHE\\P1\\bucket\\ABCD1234-\xd55c\xae00.TXT");
    REQUIRE(d != nullptr);
    CHECK(d->bucket == "bucket");
    CHECK(d->key == MakeDoc().key);
}

TEST_CASE("Unrelated local files never map to a remote document")
{
    RemoteDocumentManager mgr;
    mgr.Register(MakeDoc());
    CHECK(mgr.FindByLocalPath(L"C:\\Users\\me\\notes.txt") == nullptr);
    CHECK(mgr.FindByLocalPath(L"C:\\Cache\\p1\\bucket\\other.txt") == nullptr);
}

TEST_CASE("UpdateRemoteState changes etag after upload")
{
    RemoteDocumentManager mgr;
    RemoteDocument d = MakeDoc();
    mgr.Register(d);
    mgr.UpdateRemoteState(d.localPath, "etag-2", "Thu, 28 Aug 2026 09:00:00 GMT");
    const RemoteDocument* q = mgr.FindByLocalPath(d.localPath);
    REQUIRE(q != nullptr);
    CHECK(q->etag == "etag-2");
}

TEST_CASE("Document mapping XML roundtrip with Unicode")
{
    RemoteDocumentManager mgr;
    mgr.Register(MakeDoc());
    std::string xml = mgr.SerializeToXml();

    RemoteDocumentManager mgr2;
    REQUIRE(mgr2.DeserializeFromXml(xml));
    auto all = mgr2.All();
    REQUIRE(all.size() == 1);
    CHECK(all[0].key == MakeDoc().key);
    CHECK(all[0].localPath == MakeDoc().localPath);
    CHECK(all[0].etag == "etag-1");
    CHECK(all[0].autoUpload);
}

TEST_CASE("Remove drops the mapping")
{
    RemoteDocumentManager mgr;
    RemoteDocument d = MakeDoc();
    mgr.Register(d);
    mgr.Remove(d.localPath);
    CHECK(mgr.FindByLocalPath(d.localPath) == nullptr);
    CHECK(mgr.All().empty());
}
