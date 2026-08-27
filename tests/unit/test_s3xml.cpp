// NppS3 unit tests — S3 XML response parsing including pagination.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest.h>

#include "storage/S3Xml.h"
#include "storage/S3Client.h"

using namespace npps3;

TEST_CASE("ParseListObjectsV2: objects, prefixes, pagination")
{
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<ListBucketResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Name>test-bucket</Name>
  <Prefix>project/</Prefix>
  <KeyCount>3</KeyCount>
  <MaxKeys>2</MaxKeys>
  <Delimiter>/</Delimiter>
  <IsTruncated>true</IsTruncated>
  <NextContinuationToken>1ueGcxLPRx1Tr</NextContinuationToken>
  <Contents>
    <Key>project/index.html</Key>
    <LastModified>2026-08-01T10:00:00.000Z</LastModified>
    <ETag>&quot;fba9dede5f27731c9771645a39863328&quot;</ETag>
    <Size>1024</Size>
    <StorageClass>STANDARD</StorageClass>
  </Contents>
  <Contents>
    <Key>project/&#54620;&#44544;.txt</Key>
    <LastModified>2026-08-02T11:30:00.000Z</LastModified>
    <ETag>&quot;abc&quot;</ETag>
    <Size>7</Size>
  </Contents>
  <CommonPrefixes><Prefix>project/assets/</Prefix></CommonPrefixes>
  <CommonPrefixes><Prefix>project/logs/</Prefix></CommonPrefixes>
</ListBucketResult>)";

    auto r = ParseListObjectsV2(xml);
    REQUIRE(r.has_value());
    CHECK(r->isTruncated);
    CHECK(r->nextContinuationToken == "1ueGcxLPRx1Tr");
    REQUIRE(r->objects.size() == 2);
    CHECK(r->objects[0].key == "project/index.html");
    CHECK(r->objects[0].size == 1024);
    CHECK(r->objects[0].etag == "fba9dede5f27731c9771645a39863328"); // quotes stripped
    CHECK(r->objects[1].key == "project/\xed\x95\x9c\xea\xb8\x80.txt"); // entity-decoded UTF-8
    REQUIRE(r->commonPrefixes.size() == 2);
    CHECK(r->commonPrefixes[0] == "project/assets/");
}

TEST_CASE("ParseListObjectsV2: final page has no token")
{
    const std::string xml =
        "<ListBucketResult><IsTruncated>false</IsTruncated>"
        "<Contents><Key>a.txt</Key><Size>1</Size></Contents></ListBucketResult>";
    auto r = ParseListObjectsV2(xml);
    REQUIRE(r.has_value());
    CHECK_FALSE(r->isTruncated);
    CHECK(r->nextContinuationToken.empty());
    CHECK(r->objects.size() == 1);
}

TEST_CASE("ParseListObjectsV2 rejects malformed input")
{
    CHECK_FALSE(ParseListObjectsV2("this is not xml").has_value());
    CHECK_FALSE(ParseListObjectsV2("<Wrong/>").has_value());
}

TEST_CASE("ParseListBuckets")
{
    const std::string xml =
        "<ListAllMyBucketsResult><Buckets>"
        "<Bucket><Name>alpha</Name><CreationDate>2026-01-01T00:00:00Z</CreationDate></Bucket>"
        "<Bucket><Name>beta</Name></Bucket>"
        "</Buckets></ListAllMyBucketsResult>";
    auto r = ParseListBuckets(xml);
    REQUIRE(r.has_value());
    REQUIRE(r->size() == 2);
    CHECK((*r)[0].name == "alpha");
    CHECK((*r)[1].name == "beta");
}

TEST_CASE("ParseErrorBody")
{
    auto e = ParseErrorBody(
        "<Error><Code>NoSuchKey</Code><Message>The specified key does not exist.</Message></Error>");
    REQUIRE(e.has_value());
    CHECK(e->code == "NoSuchKey");
    CHECK_FALSE(ParseErrorBody("<Ok/>").has_value());
    CHECK_FALSE(ParseErrorBody("").has_value());
}

TEST_CASE("ParseCopyObjectEtag")
{
    auto e = ParseCopyObjectEtag(
        "<CopyObjectResult><ETag>\"9b2cf535f27731c974343645a3985328\"</ETag>"
        "<LastModified>2026-08-27T00:00:00Z</LastModified></CopyObjectResult>");
    REQUIRE(e.has_value());
    CHECK(*e == "9b2cf535f27731c974343645a3985328");
}

TEST_CASE("Error classification and retry policy")
{
    CHECK(ClassifyS3Error(403, "AccessDenied") == ErrorKind::AccessDenied);
    CHECK(ClassifyS3Error(403, "SignatureDoesNotMatch") == ErrorKind::InvalidCredentials);
    CHECK(ClassifyS3Error(403, "InvalidAccessKeyId") == ErrorKind::InvalidCredentials);
    CHECK(ClassifyS3Error(404, "NoSuchKey") == ErrorKind::NoSuchKey);
    CHECK(ClassifyS3Error(404, "NoSuchBucket") == ErrorKind::NoSuchBucket);
    CHECK(ClassifyS3Error(429, "") == ErrorKind::Throttled);
    CHECK(ClassifyS3Error(503, "SlowDown") == ErrorKind::Throttled);

    StorageError throttled{.kind = ErrorKind::Throttled, .httpStatus = 429};
    CHECK(throttled.Retryable());
    StorageError denied{.kind = ErrorKind::AccessDenied, .httpStatus = 403, .s3Code = "AccessDenied"};
    CHECK_FALSE(denied.Retryable());
    StorageError server{.kind = ErrorKind::Http, .httpStatus = 500};
    CHECK(server.Retryable());
    StorageError network{.kind = ErrorKind::Network};
    CHECK(network.Retryable());
    StorageError cancelled{.kind = ErrorKind::Cancelled};
    CHECK_FALSE(cancelled.Retryable());
}

TEST_CASE("StorageError.Describe is safe and informative")
{
    StorageError e{.kind = ErrorKind::NoSuchKey, .httpStatus = 404, .s3Code = "NoSuchKey",
                   .message = "The specified key does not exist."};
    std::string d = e.Describe();
    CHECK(d.find("NoSuchKey") != std::string::npos);
    CHECK(d.find("404") != std::string::npos);
}
