// NppS3 unit tests — error text follows the UI language, not just the title.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <doctest.h>

#include "ui/ErrorText.h"
#include "ui/I18n.h"

#include <vector>

using namespace npps3;

namespace {

// A translated body carries the target script. Latin runs are not evidence of
// a leak: Korean keeps terms like TLS, ETag and Secret Access Key as-is.
bool HasScriptFor(Lang lang, const std::wstring& text)
{
    for (wchar_t c : text)
    {
        switch (lang)
        {
        case Lang::KO: if (c >= 0xAC00 && c <= 0xD7A3) return true; break;
        case Lang::JA: if (c >= 0x3040 && c <= 0x30FF) return true; break;
        case Lang::ZH: if (c >= 0x4E00 && c <= 0x9FFF) return true; break;
        case Lang::RU: if (c >= 0x0400 && c <= 0x04FF) return true; break;
        default:       return true; // English needs no script check
        }
    }
    return false;
}

struct LangGuard
{
    Lang previous = CurrentLanguage();
    ~LangGuard() { SetLanguage(previous); }
};

} // namespace

TEST_CASE("DescribeErrorLocalized translates the message body")
{
    LangGuard guard;

    StorageError credentials;
    credentials.kind = ErrorKind::InvalidCredentials;
    credentials.httpStatus = 403;
    credentials.s3Code = "SignatureDoesNotMatch";
    credentials.message = "The request signature we calculated does not match.";

    SetLanguage(Lang::EN);
    const std::wstring en = DescribeErrorLocalized(credentials);
    SetLanguage(Lang::KO);
    const std::wstring ko = DescribeErrorLocalized(credentials);

    CHECK(en != ko);                       // the body actually changed
    CHECK(HasScriptFor(Lang::KO, ko));     // and the body is really Korean

    // Machine identifiers stay put in both languages: a report needs them.
    for (const std::wstring& text : {en, ko})
    {
        CHECK(text.find(L"SignatureDoesNotMatch") != std::wstring::npos);
        CHECK(text.find(L"HTTP 403") != std::wstring::npos);
    }
}

TEST_CASE("DescribeErrorLocalized covers every error the plugin raises itself")
{
    LangGuard guard;
    SetLanguage(Lang::KO);

    // Walking the enum keeps a newly added detail from silently falling back
    // to the generic wording.
    for (int i = static_cast<int>(ErrorDetail::Timeout);
         i <= static_cast<int>(ErrorDetail::NoStoredCredential); ++i)
    {
        StorageError e;
        e.kind = ErrorKind::Internal;
        e.detail = static_cast<ErrorDetail>(i);
        e.message = "english diagnostic text";
        const std::wstring text = DescribeErrorLocalized(e);

        INFO("ErrorDetail = ", i);
        CHECK_FALSE(text.empty());
        CHECK(HasScriptFor(Lang::KO, text));
        CHECK(text.find(L"english") == std::wstring::npos);
    }
}

TEST_CASE("DescribeErrorLocalized translates service errors by kind")
{
    LangGuard guard;
    SetLanguage(Lang::KO);

    const ErrorKind kinds[] = {
        ErrorKind::Network,      ErrorKind::AccessDenied, ErrorKind::InvalidCredentials,
        ErrorKind::NoSuchBucket, ErrorKind::NoSuchKey,    ErrorKind::Conflict,
        ErrorKind::Throttled,    ErrorKind::Cancelled,    ErrorKind::LocalIo,
        ErrorKind::Internal,
    };
    for (ErrorKind kind : kinds)
    {
        StorageError e;
        e.kind = kind;
        const std::wstring text = DescribeErrorLocalized(e);
        INFO("ErrorKind = ", static_cast<int>(kind));
        CHECK(HasScriptFor(Lang::KO, text));
    }
}

TEST_CASE("DescribeErrorLocalized keeps the service wording for unknown codes")
{
    LangGuard guard;
    SetLanguage(Lang::KO);

    // Nothing is known about this code, so dropping the server's own sentence
    // would throw away the only explanation there is.
    StorageError e;
    e.kind = ErrorKind::Http;
    e.httpStatus = 400;
    e.s3Code = "SomeUnmappedCode";
    e.message = "Bucket policy rejected the request.";
    const std::wstring text = DescribeErrorLocalized(e);

    CHECK(text.find(L"Bucket policy rejected") != std::wstring::npos);
    CHECK(text.find(L"SomeUnmappedCode") != std::wstring::npos);

    // A recognized kind must not append the English sentence.
    StorageError known;
    known.kind = ErrorKind::AccessDenied;
    known.httpStatus = 403;
    known.s3Code = "AccessDenied";
    known.message = "Access Denied";
    CHECK(DescribeErrorLocalized(known).find(L"Access Denied") == std::wstring::npos);
}

TEST_CASE("DescribeErrorLocalized renders every UI language")
{
    LangGuard guard;

    StorageError e;
    e.kind = ErrorKind::LocalIo;
    e.detail = ErrorDetail::LocalWriteFailed;

    std::vector<std::wstring> rendered;
    for (Lang lang : {Lang::EN, Lang::KO, Lang::JA, Lang::ZH, Lang::RU})
    {
        SetLanguage(lang);
        std::wstring text = DescribeErrorLocalized(e);
        CHECK_FALSE(text.empty());
        rendered.push_back(text);
    }
    // Five distinct translations, so no language silently falls back.
    for (size_t i = 0; i < rendered.size(); ++i)
        for (size_t j = i + 1; j < rendered.size(); ++j)
            CHECK(rendered[i] != rendered[j]);
}
