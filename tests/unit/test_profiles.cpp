// NppS3 unit tests — profile serialization (never containing secrets).
// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest.h>

#include "config/ProfileManager.h"

using namespace npps3;

static Profile MakeProfile()
{
    Profile p;
    p.id = "11111111-2222-3333-4444-555555555555";
    p.name = "R2 Test";
    p.provider = Provider::CloudflareR2;
    p.endpoint = "https://example.r2.cloudflarestorage.com";
    p.region = "auto";
    p.accessKeyId = "AKEXAMPLE";
    p.defaultBucket = "my-bucket";
    p.defaultPrefix = "project/";
    p.pathStyle = true;
    p.autoUploadOnSave = true;
    return p;
}

TEST_CASE("Profile XML roundtrip")
{
    ProfileManager mgr;
    Profile p = MakeProfile();
    mgr.AddOrUpdate(p);
    mgr.Settings().activeProfileId = p.id;
    mgr.Settings().staleCacheDays = 7;

    std::string xml = mgr.SerializeToXml();

    ProfileManager mgr2;
    REQUIRE(mgr2.DeserializeFromXml(xml));
    REQUIRE(mgr2.Profiles().size() == 1);
    const Profile& q = mgr2.Profiles()[0];
    CHECK(q.id == p.id);
    CHECK(q.name == p.name);
    CHECK(q.provider == Provider::CloudflareR2);
    CHECK(q.endpoint == p.endpoint);
    CHECK(q.region == "auto");
    CHECK(q.accessKeyId == p.accessKeyId);
    CHECK(q.defaultBucket == p.defaultBucket);
    CHECK(q.defaultPrefix == p.defaultPrefix);
    CHECK(q.pathStyle);
    CHECK(q.autoUploadOnSave);
    CHECK(mgr2.Settings().activeProfileId == p.id);
    CHECK(mgr2.Settings().staleCacheDays == 7);
}

TEST_CASE("Profile XML contains no secret fields")
{
    ProfileManager mgr;
    mgr.AddOrUpdate(MakeProfile());
    std::string xml = mgr.SerializeToXml();
    CHECK(xml.find("secret") == std::string::npos);
    CHECK(xml.find("Secret") == std::string::npos);
    CHECK(xml.find("password") == std::string::npos);
}

TEST_CASE("Unicode profile fields survive roundtrip")
{
    ProfileManager mgr;
    Profile p = MakeProfile();
    p.name = "\xed\x95\x9c\xea\xb8\x80 \xed\x94\x84\xeb\xa1\x9c\xed\x95\x84"; // 한글 프로필
    mgr.AddOrUpdate(p);
    ProfileManager mgr2;
    REQUIRE(mgr2.DeserializeFromXml(mgr.SerializeToXml()));
    CHECK(mgr2.Profiles()[0].name == p.name);
}

TEST_CASE("AddOrUpdate replaces by id; Remove clears active profile")
{
    ProfileManager mgr;
    Profile p = MakeProfile();
    mgr.AddOrUpdate(p);
    p.name = "Renamed";
    mgr.AddOrUpdate(p);
    REQUIRE(mgr.Profiles().size() == 1);
    CHECK(mgr.Profiles()[0].name == "Renamed");

    mgr.Settings().activeProfileId = p.id;
    mgr.Remove(p.id);
    CHECK(mgr.Profiles().empty());
    CHECK(mgr.Settings().activeProfileId.empty());
}

TEST_CASE("GenerateId yields unique GUID-shaped ids")
{
    std::string a = ProfileManager::GenerateId();
    std::string b = ProfileManager::GenerateId();
    CHECK(a.size() == 36);
    CHECK(a != b);
}

TEST_CASE("Malformed profile XML is rejected")
{
    ProfileManager mgr;
    CHECK_FALSE(mgr.DeserializeFromXml("garbage"));
    CHECK_FALSE(mgr.DeserializeFromXml("<Other/>"));
}
