// NppS3 unit tests — cache path mapping for hostile object keys.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest.h>

#include "cache/CacheManager.h"

#include <algorithm>

using namespace npps3;

TEST_CASE("SanitizeComponent replaces invalid Windows characters")
{
    CHECK(CacheManager::SanitizeComponent(L"a<b>c:d\"e/f\\g|h?i*j", 100) == L"a_b_c_d_e_f_g_h_i_j");
    CHECK(CacheManager::SanitizeComponent(L"normal.txt", 100) == L"normal.txt");
}

TEST_CASE("SanitizeComponent handles reserved device names")
{
    CHECK(CacheManager::SanitizeComponent(L"CON", 100) == L"_CON");
    CHECK(CacheManager::SanitizeComponent(L"aux.txt", 100) == L"_aux.txt");
    CHECK(CacheManager::SanitizeComponent(L"console.txt", 100) == L"console.txt");
}

TEST_CASE("SanitizeComponent strips trailing dots/spaces and never returns empty")
{
    CHECK(CacheManager::SanitizeComponent(L"name...", 100) == L"name");
    CHECK(CacheManager::SanitizeComponent(L"   ", 100) == L"_");
    CHECK(CacheManager::SanitizeComponent(L"", 100) == L"_");
}

TEST_CASE("SanitizeComponent truncation keeps the extension")
{
    std::wstring longName(100, L'x');
    longName += L".json";
    std::wstring s = CacheManager::SanitizeComponent(longName, 20);
    CHECK(s.size() == 20);
    CHECK(s.substr(s.size() - 5) == L".json");
}

TEST_CASE("LocalPathFor is deterministic and collision-resistant")
{
    CacheManager cache;
    cache.SetRoot(L"C:\\Cache");

    std::wstring a1 = cache.LocalPathFor("profile-1", "bucket", "dir/file.txt");
    std::wstring a2 = cache.LocalPathFor("profile-1", "bucket", "dir/file.txt");
    CHECK(a1 == a2);

    // Keys that sanitize identically must still map to different files.
    std::wstring b = cache.LocalPathFor("profile-1", "bucket", "dir/file?.txt");
    std::wstring c = cache.LocalPathFor("profile-1", "bucket", "dir/file*.txt");
    CHECK(b != c);
    CHECK(a1 != b);

    // Same key in another bucket or profile is a different file.
    CHECK(cache.LocalPathFor("profile-1", "other", "dir/file.txt") != a1);
    CHECK(cache.LocalPathFor("profile-2", "bucket", "dir/file.txt") != a1);
}

TEST_CASE("LocalPathFor keeps extension for lexer detection")
{
    CacheManager cache;
    cache.SetRoot(L"C:\\Cache");
    std::wstring p = cache.LocalPathFor("p", "b", "\xed\x95\x9c\xea\xb8\x80/\xed\x85\x8c\xec\x8a\xa4\xed\x8a\xb8.json");
    CHECK(p.substr(p.size() - 5) == L".json");
    CHECK(p.find(L"C:\\Cache\\") == 0);
    // No path component from the key may escape the cache root.
    CHECK(p.find(L"..") == std::wstring::npos);
}

TEST_CASE("LocalPathFor stays well under Windows MAX_PATH pressure")
{
    CacheManager cache;
    cache.SetRoot(L"C:\\Users\\someone\\AppData\\Local\\NppS3\\Cache");
    std::string longKey(500, 'k');
    longKey += "/leaf-with-a-rather-long-name-exceeding-limits-1234567890.markdown";
    std::wstring p = cache.LocalPathFor("profile-guid-here", "bucket-name", longKey);
    CHECK(p.size() < 260);
}

TEST_CASE("RelativePathForKey maps object keys onto a safe local tree")
{
    SUBCASE("nested keys become nested directories")
    {
        CHECK(CacheManager::RelativePathForKey("a/b/c.txt") == L"a\\b\\c.txt");
        CHECK(CacheManager::RelativePathForKey("flat.txt") == L"flat.txt");
    }

    SUBCASE("a folder marker maps to the directory, not a file")
    {
        CHECK(CacheManager::RelativePathForKey("a/b/") == L"a\\b");
        CHECK(CacheManager::RelativePathForKey("").empty());
    }

    SUBCASE("empty segments collapse instead of producing an empty level")
    {
        CHECK(CacheManager::RelativePathForKey("a//b.txt") == L"a\\b.txt");
    }

    SUBCASE("characters Windows forbids in a name are replaced")
    {
        std::wstring p = CacheManager::RelativePathForKey("dir/a:b?c*d|e.txt");
        CHECK(p == L"dir\\a_b_c_d_e.txt");
        // Separators come from the key's '/', never from its content.
        CHECK(std::count(p.begin(), p.end(), L'\\') == 1);
    }

    SUBCASE("a key cannot escape the destination directory")
    {
        std::wstring p = CacheManager::RelativePathForKey("../../etc/passwd");
        CHECK(p.find(L"..") == std::wstring::npos);
    }

    SUBCASE("unicode segments survive")
    {
        // 한글/테스트.json
        CHECK(CacheManager::RelativePathForKey(
                  "\xed\x95\x9c\xea\xb8\x80/\xed\x85\x8c\xec\x8a\xa4\xed\x8a\xb8.json") ==
              L"\ud55c\uae00\\\ud14c\uc2a4\ud2b8.json");
    }
}

TEST_CASE("ExtendedPath lifts the MAX_PATH limit only when needed")
{
    CHECK(CacheManager::ExtendedPath(L"C:\\short\\path.txt") == L"C:\\short\\path.txt");

    const std::wstring deep = L"C:\\dir\\" + std::wstring(300, L'x') + L".txt";
    CHECK(CacheManager::ExtendedPath(deep) == L"\\\\?\\" + deep);
    // Already-prefixed paths are left alone.
    CHECK(CacheManager::ExtendedPath(L"\\\\?\\" + deep) == L"\\\\?\\" + deep);

    const std::wstring unc = L"\\\\server\\share\\" + std::wstring(300, L'y') + L".txt";
    CHECK(CacheManager::ExtendedPath(unc) ==
          L"\\\\?\\UNC\\server\\share\\" + std::wstring(300, L'y') + L".txt");
}
