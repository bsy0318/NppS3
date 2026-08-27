// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Mime.h"
#include "StringUtil.h"

#include <unordered_map>

namespace npps3 {

std::string MimeTypeForKey(std::string_view keyOrName)
{
    static const std::unordered_map<std::string, std::string> kTable = {
        {"txt", "text/plain"},
        {"md", "text/markdown"},
        {"html", "text/html"},
        {"htm", "text/html"},
        {"css", "text/css"},
        {"csv", "text/csv"},
        {"xml", "application/xml"},
        {"js", "text/javascript"},
        {"mjs", "text/javascript"},
        {"json", "application/json"},
        {"yaml", "application/yaml"},
        {"yml", "application/yaml"},
        {"toml", "application/toml"},
        {"ini", "text/plain"},
        {"log", "text/plain"},
        {"sh", "application/x-sh"},
        {"py", "text/x-python"},
        {"pdf", "application/pdf"},
        {"zip", "application/zip"},
        {"gz", "application/gzip"},
        {"tar", "application/x-tar"},
        {"7z", "application/x-7z-compressed"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"webp", "image/webp"},
        {"svg", "image/svg+xml"},
        {"ico", "image/vnd.microsoft.icon"},
        {"bmp", "image/bmp"},
        {"avif", "image/avif"},
        {"mp3", "audio/mpeg"},
        {"wav", "audio/wav"},
        {"ogg", "audio/ogg"},
        {"mp4", "video/mp4"},
        {"webm", "video/webm"},
        {"woff", "font/woff"},
        {"woff2", "font/woff2"},
        {"ttf", "font/ttf"},
        {"otf", "font/otf"},
        {"wasm", "application/wasm"},
        {"doc", "application/msword"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xls", "application/vnd.ms-excel"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"ppt", "application/vnd.ms-powerpoint"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    };

    size_t slash = keyOrName.find_last_of("/\\");
    std::string_view name = slash == std::string_view::npos ? keyOrName : keyOrName.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size())
        return "application/octet-stream";

    std::string ext = ToLowerAscii(name.substr(dot + 1));
    auto it = kTable.find(ext);
    return it != kTable.end() ? it->second : "application/octet-stream";
}

} // namespace npps3
