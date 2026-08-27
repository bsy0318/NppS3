// NppS3 unit tests — crypto primitives and SigV4 against published vectors.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest.h>

#include "storage/SigV4.h"
#include "util/Hash.h"
#include "util/StringUtil.h"

using namespace npps3;

TEST_CASE("SHA-256 FIPS 180 vector")
{
    CHECK(Sha256Hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("HMAC-SHA256 RFC 4231 test case 2")
{
    auto mac = HmacSha256("Jefe", 4, "what do ya want for nothing?");
    CHECK(HexLower(mac.data(), mac.size()) ==
          "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

// AWS Signature Version 4 complete example from the AWS General Reference
// ("Signature Calculations", GET iam ListUsers).
static SigV4Request AwsExampleRequest()
{
    SigV4Request req;
    req.method = "GET";
    req.canonicalUri = "/";
    req.query.emplace("Action", "ListUsers");
    req.query.emplace("Version", "2010-05-08");
    req.headers["content-type"] = "application/x-www-form-urlencoded; charset=utf-8";
    req.headers["host"] = "iam.amazonaws.com";
    req.headers["x-amz-date"] = "20150830T123600Z";
    req.payloadHash = kEmptyPayloadSha256;
    return req;
}

static const SigV4Credentials kAwsExampleCreds{
    "AKIDEXAMPLE",
    "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY",
    "us-east-1",
    "iam",
};

TEST_CASE("SigV4 canonical request matches AWS example")
{
    std::string signedHeaders;
    std::string canonical = SigV4CanonicalRequest(AwsExampleRequest(), &signedHeaders);
    CHECK(signedHeaders == "content-type;host;x-amz-date");
    CHECK(Sha256Hex(canonical) ==
          "f536975d06c0309214f805bb90ccff089219ecd68b2577efef23edd43b7e1a59");
}

TEST_CASE("SigV4 string-to-sign and signature match AWS example")
{
    SigV4Request req = AwsExampleRequest();
    const std::string amzDate = "20150830T123600Z";

    std::string canonical = SigV4CanonicalRequest(req, nullptr);
    std::string scope = SigV4Scope(amzDate, kAwsExampleCreds);
    CHECK(scope == "20150830/us-east-1/iam/aws4_request");

    std::string sts = SigV4StringToSign(canonical, amzDate, scope);
    CHECK(sts ==
          "AWS4-HMAC-SHA256\n"
          "20150830T123600Z\n"
          "20150830/us-east-1/iam/aws4_request\n"
          "f536975d06c0309214f805bb90ccff089219ecd68b2577efef23edd43b7e1a59");

    CHECK(SigV4Signature(sts, amzDate, kAwsExampleCreds) ==
          "5d672d79c15b13162d9279b0855cfba6789a8edb4c82c400e06b5924a6f2b5d7");
}

TEST_CASE("SigV4 Authorization header assembles all parts")
{
    std::string auth = SigV4Authorization(AwsExampleRequest(), kAwsExampleCreds,
                                          "20150830T123600Z");
    CHECK(auth ==
          "AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/20150830/us-east-1/iam/aws4_request, "
          "SignedHeaders=content-type;host;x-amz-date, "
          "Signature=5d672d79c15b13162d9279b0855cfba6789a8edb4c82c400e06b5924a6f2b5d7");
}

TEST_CASE("SigV4 sorts query parameters canonically")
{
    SigV4Request req;
    req.method = "GET";
    req.canonicalUri = "/bucket";
    req.query.emplace("prefix", "b");
    req.query.emplace("list-type", "2");
    req.query.emplace("delimiter", "/");
    req.payloadHash = kEmptyPayloadSha256;
    req.headers["host"] = "example.com";
    req.headers["x-amz-date"] = "20260101T000000Z";

    std::string canonical = SigV4CanonicalRequest(req, nullptr);
    // Query line is the 3rd line of the canonical request.
    size_t p1 = canonical.find('\n');
    size_t p2 = canonical.find('\n', p1 + 1);
    size_t p3 = canonical.find('\n', p2 + 1);
    std::string queryLine = canonical.substr(p2 + 1, p3 - p2 - 1);
    CHECK(queryLine == "delimiter=%2F&list-type=2&prefix=b");
}

TEST_CASE("AmzDateNow shape")
{
    std::string d = AmzDateNow();
    REQUIRE(d.size() == 16);
    CHECK(d[8] == 'T');
    CHECK(d.back() == 'Z');
}
