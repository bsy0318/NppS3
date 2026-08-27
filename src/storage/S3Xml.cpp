// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "S3Xml.h"

#include <tinyxml2.h>

namespace npps3 {
namespace {

std::string StripQuotes(std::string s)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

const char* ChildText(const tinyxml2::XMLElement* parent, const char* name)
{
    if (!parent)
        return nullptr;
    const tinyxml2::XMLElement* el = parent->FirstChildElement(name);
    return el ? el->GetText() : nullptr;
}

std::string ChildTextOr(const tinyxml2::XMLElement* parent, const char* name)
{
    const char* t = ChildText(parent, name);
    return t ? t : "";
}

} // namespace

std::optional<std::vector<BucketInfo>> ParseListBuckets(const std::string& xml)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        return std::nullopt;
    const tinyxml2::XMLElement* root = doc.FirstChildElement("ListAllMyBucketsResult");
    if (!root)
        return std::nullopt;

    std::vector<BucketInfo> out;
    const tinyxml2::XMLElement* buckets = root->FirstChildElement("Buckets");
    for (const tinyxml2::XMLElement* b = buckets ? buckets->FirstChildElement("Bucket") : nullptr;
         b; b = b->NextSiblingElement("Bucket"))
    {
        BucketInfo info;
        info.name = ChildTextOr(b, "Name");
        info.creationDate = ChildTextOr(b, "CreationDate");
        if (!info.name.empty())
            out.push_back(std::move(info));
    }
    return out;
}

std::optional<ListObjectsResult> ParseListObjectsV2(const std::string& xml)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        return std::nullopt;
    const tinyxml2::XMLElement* root = doc.FirstChildElement("ListBucketResult");
    if (!root)
        return std::nullopt;

    ListObjectsResult out;
    out.isTruncated = ChildTextOr(root, "IsTruncated") == "true";
    out.nextContinuationToken = ChildTextOr(root, "NextContinuationToken");

    for (const tinyxml2::XMLElement* c = root->FirstChildElement("Contents");
         c; c = c->NextSiblingElement("Contents"))
    {
        ObjectInfo obj;
        obj.key = ChildTextOr(c, "Key");
        obj.lastModified = ChildTextOr(c, "LastModified");
        obj.etag = StripQuotes(ChildTextOr(c, "ETag"));
        obj.storageClass = ChildTextOr(c, "StorageClass");
        const char* size = ChildText(c, "Size");
        if (size)
            obj.size = ::_strtoui64(size, nullptr, 10);
        if (!obj.key.empty())
            out.objects.push_back(std::move(obj));
    }

    for (const tinyxml2::XMLElement* p = root->FirstChildElement("CommonPrefixes");
         p; p = p->NextSiblingElement("CommonPrefixes"))
    {
        std::string cp = ChildTextOr(p, "Prefix");
        if (!cp.empty())
            out.commonPrefixes.push_back(std::move(cp));
    }
    return out;
}

std::optional<S3ErrorBody> ParseErrorBody(const std::string& xml)
{
    if (xml.empty())
        return std::nullopt;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        return std::nullopt;
    const tinyxml2::XMLElement* err = doc.FirstChildElement("Error");
    if (!err)
        return std::nullopt;
    S3ErrorBody out;
    out.code = ChildTextOr(err, "Code");
    out.message = ChildTextOr(err, "Message");
    return out;
}

std::optional<std::string> ParseCopyObjectEtag(const std::string& xml)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        return std::nullopt;
    const tinyxml2::XMLElement* res = doc.FirstChildElement("CopyObjectResult");
    if (!res)
        return std::nullopt;
    return StripQuotes(ChildTextOr(res, "ETag"));
}

} // namespace npps3
