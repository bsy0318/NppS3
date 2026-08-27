// NppS3 unit tests
// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest.h>

#include "util/Mime.h"

using namespace npps3;

TEST_CASE("MIME detection for common types")
{
    CHECK(MimeTypeForKey("index.html") == "text/html");
    CHECK(MimeTypeForKey("style.css") == "text/css");
    CHECK(MimeTypeForKey("config.json") == "application/json");
    CHECK(MimeTypeForKey("logo.png") == "image/png");
    CHECK(MimeTypeForKey("script.js") == "text/javascript");
    CHECK(MimeTypeForKey("readme.md") == "text/markdown");
}

TEST_CASE("MIME detection is case-insensitive and path-aware")
{
    CHECK(MimeTypeForKey("A/B/PHOTO.JPG") == "image/jpeg");
    CHECK(MimeTypeForKey("dir.with.dots/file.TXT") == "text/plain");
}

TEST_CASE("Unknown or missing extensions fall back to octet-stream")
{
    CHECK(MimeTypeForKey("binary.xyz123") == "application/octet-stream");
    CHECK(MimeTypeForKey("noextension") == "application/octet-stream");
    CHECK(MimeTypeForKey("trailingdot.") == "application/octet-stream");
    CHECK(MimeTypeForKey("") == "application/octet-stream");
}
