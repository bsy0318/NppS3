// NppS3 unit tests
// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest.h>

#include "util/StringUtil.h"

using namespace npps3;

TEST_CASE("Utf8/Wide roundtrip preserves Unicode")
{
    const std::string utf8 = "\xed\x95\x9c\xea\xb8\x80/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.txt"; // 한글/日本語.txt
    std::wstring wide = Utf8ToWide(utf8);
    CHECK(WideToUtf8(wide) == utf8);
    CHECK(!wide.empty());
}

TEST_CASE("UriEncode follows RFC 3986 unreserved set")
{
    CHECK(UriEncode("AZaz09-_.~", false) == "AZaz09-_.~");
    CHECK(UriEncode("a b", false) == "a%20b");
    CHECK(UriEncode("a+b", false) == "a%2Bb");
    CHECK(UriEncode("a=b&c", false) == "a%3Db%26c");
}

TEST_CASE("UriEncode keeps or encodes slash by mode")
{
    CHECK(UriEncode("folder/file.txt", true) == "folder/file.txt");
    CHECK(UriEncode("folder/file.txt", false) == "folder%2Ffile.txt");
}

TEST_CASE("UriEncode encodes UTF-8 bytes of non-ASCII keys")
{
    // "한" is ED 95 9C in UTF-8
    CHECK(UriEncode("\xed\x95\x9c", false) == "%ED%95%9C");
}

TEST_CASE("UriEncode special S3 key characters")
{
    CHECK(UriEncode("special chars/#test?.txt", true) == "special%20chars/%23test%3F.txt");
}

TEST_CASE("MaskSensitive never reveals short values")
{
    CHECK(MaskSensitive("abcd") == "****");
    CHECK(MaskSensitive("") == "****");
    std::string masked = MaskSensitive("AKIAIOSFODNN7EXAMPLE");
    CHECK(masked == "AK****LE");
    CHECK(masked.find("IOSFODNN") == std::string::npos);
}

TEST_CASE("SplitKey handles separators")
{
    auto parts = SplitKey("a/b/c", '/');
    REQUIRE(parts.size() == 3);
    CHECK(parts[0] == "a");
    CHECK(parts[2] == "c");
    CHECK(SplitKey("", '/').empty());
    CHECK(SplitKey("abc", '/').size() == 1);
    CHECK(SplitKey("a//b/", '/').size() == 2); // empty segments skipped
}

TEST_CASE("Trim and case helpers")
{
    CHECK(Trim("  x \r\n") == "x");
    CHECK(ToLowerAscii("Content-TYPE") == "content-type");
    CHECK(StartsWith("https://x", "https://"));
    CHECK(EndsWith("file.txt", ".txt"));
    CHECK(EqualsNoCase(L"AbC", L"aBc"));
}
