// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Dialogs.h"

#include "I18n.h"
#include "resource.h"
#include "../plugin/NppS3Plugin.h"
#include "../util/Mime.h"
#include "../util/StringUtil.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace npps3 {
namespace {

constexpr UINT WM_TESTRESULT = WM_APP + 51; // wParam: ok, lParam: std::wstring* detail (owned)

std::wstring GetDlgText(HWND dlg, int id)
{
    HWND ctl = ::GetDlgItem(dlg, id);
    int len = ::GetWindowTextLengthW(ctl);
    std::wstring out(static_cast<size_t>(len), L'\0');
    if (len > 0)
        ::GetWindowTextW(ctl, out.data(), len + 1);
    return out;
}

void SetDlgText(HWND dlg, int id, const std::wstring& text)
{
    ::SetWindowTextW(::GetDlgItem(dlg, id), text.c_str());
}

// ------------------------------------------------------------- profiles dialog

struct ProfilesState
{
    bool creatingNew = false;
    bool testRunning = false;
    std::string editingId;            // empty while creating a new profile
    std::string* connectId = nullptr; // set when the user double-clicks to connect
    bool connectRequested = false;
};

void LocalizeProfilesDialog(HWND dlg)
{
    ::SetWindowTextW(dlg, T(StrId::DlgProfilesTitle));
    SetDlgText(dlg, IDC_LBL_LIST, T(StrId::LblProfileList));
    SetDlgText(dlg, IDC_LBL_NAME, T(StrId::LblProfileName));
    SetDlgText(dlg, IDC_LBL_PROVIDER, T(StrId::LblProvider));
    SetDlgText(dlg, IDC_LBL_ENDPOINT, T(StrId::LblEndpoint));
    SetDlgText(dlg, IDC_LBL_REGION, T(StrId::LblRegion));
    SetDlgText(dlg, IDC_LBL_ACCESSKEY, T(StrId::LblAccessKey));
    SetDlgText(dlg, IDC_LBL_SECRET, T(StrId::LblSecretKey));
    SetDlgText(dlg, IDC_LBL_SECRETHINT, T(StrId::LblSecretHint));
    SetDlgText(dlg, IDC_LBL_BUCKET, T(StrId::LblDefaultBucket));
    SetDlgText(dlg, IDC_LBL_PREFIX, T(StrId::LblDefaultPrefix));
    SetDlgText(dlg, IDC_CHK_PATHSTYLE, T(StrId::ChkPathStyle));
    SetDlgText(dlg, IDC_CHK_AUTOUPLOAD, T(StrId::ChkAutoUpload));
    SetDlgText(dlg, IDC_BTN_NEW, T(StrId::BtnNew));
    SetDlgText(dlg, IDC_BTN_DUPLICATE, T(StrId::BtnDuplicate));
    SetDlgText(dlg, IDC_BTN_DELETE, T(StrId::BtnDelete));
    SetDlgText(dlg, IDC_BTN_SAVE, T(StrId::BtnSave));
    SetDlgText(dlg, IDC_BTN_TEST, T(StrId::BtnTest));
    SetDlgText(dlg, IDCANCEL, T(StrId::BtnClose));
}

void FillProfileList(HWND dlg, const std::string& selectId)
{
    HWND list = ::GetDlgItem(dlg, IDC_PROFILE_LIST);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    auto& profiles = NppS3Plugin::Instance().Profiles().Profiles();
    int select = -1;
    for (size_t i = 0; i < profiles.size(); ++i)
    {
        ::SendMessageW(list, LB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(Utf8ToWide(profiles[i].name).c_str()));
        if (profiles[i].id == selectId)
            select = static_cast<int>(i);
    }
    if (select < 0 && !profiles.empty())
        select = 0;
    if (select >= 0)
        ::SendMessageW(list, LB_SETCURSEL, select, 0);
}

void LoadProfileFields(HWND dlg, ProfilesState* st)
{
    auto& mgr = NppS3Plugin::Instance().Profiles();
    HWND list = ::GetDlgItem(dlg, IDC_PROFILE_LIST);
    int sel = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
    const auto& profiles = mgr.Profiles();

    Profile p; // defaults for "new"
    if (!st->creatingNew && sel >= 0 && sel < static_cast<int>(profiles.size()))
    {
        p = profiles[static_cast<size_t>(sel)];
        st->editingId = p.id;
    }
    else
    {
        st->editingId.clear();
    }

    SetDlgText(dlg, IDC_ED_NAME, Utf8ToWide(p.name));
    ::SendDlgItemMessageW(dlg, IDC_CB_PROVIDER, CB_SETCURSEL, static_cast<int>(p.provider), 0);
    SetDlgText(dlg, IDC_ED_ENDPOINT, Utf8ToWide(p.endpoint));
    SetDlgText(dlg, IDC_ED_REGION, Utf8ToWide(p.region));
    SetDlgText(dlg, IDC_ED_ACCESSKEY, Utf8ToWide(p.accessKeyId));

    // The secret is shown as a normal Windows password field (dots) rather
    // than left blank, so the user can see it is set and edit it in place.
    std::string secret;
    if (!st->editingId.empty())
    {
        if (auto stored = mgr.GetSecret(st->editingId))
            secret = *stored;
    }
    std::wstring wideSecret = Utf8ToWide(secret);
    SetDlgText(dlg, IDC_ED_SECRET, wideSecret);
    if (!wideSecret.empty())
        ::SecureZeroMemory(wideSecret.data(), wideSecret.size() * sizeof(wchar_t));
    if (!secret.empty())
        ::SecureZeroMemory(secret.data(), secret.size());

    SetDlgText(dlg, IDC_ED_BUCKET, Utf8ToWide(p.defaultBucket));
    SetDlgText(dlg, IDC_ED_PREFIX, Utf8ToWide(p.defaultPrefix));
    ::CheckDlgButton(dlg, IDC_CHK_PATHSTYLE, p.pathStyle ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dlg, IDC_CHK_AUTOUPLOAD, p.autoUploadOnSave ? BST_CHECKED : BST_UNCHECKED);
}

Profile GatherProfileFields(HWND dlg, const ProfilesState* st)
{
    Profile p;
    p.id = st->editingId;
    p.name = WideToUtf8(GetDlgText(dlg, IDC_ED_NAME));
    int prov = static_cast<int>(::SendDlgItemMessageW(dlg, IDC_CB_PROVIDER, CB_GETCURSEL, 0, 0));
    p.provider = prov == 0 ? Provider::AmazonS3 : prov == 1 ? Provider::CloudflareR2 : Provider::CustomS3;
    p.endpoint = Trim(WideToUtf8(GetDlgText(dlg, IDC_ED_ENDPOINT)));
    p.region = Trim(WideToUtf8(GetDlgText(dlg, IDC_ED_REGION)));
    p.accessKeyId = Trim(WideToUtf8(GetDlgText(dlg, IDC_ED_ACCESSKEY)));
    p.defaultBucket = Trim(WideToUtf8(GetDlgText(dlg, IDC_ED_BUCKET)));
    p.defaultPrefix = WideToUtf8(GetDlgText(dlg, IDC_ED_PREFIX));
    p.pathStyle = ::IsDlgButtonChecked(dlg, IDC_CHK_PATHSTYLE) == BST_CHECKED;
    p.autoUploadOnSave = ::IsDlgButtonChecked(dlg, IDC_CHK_AUTOUPLOAD) == BST_CHECKED;
    if (p.region.empty())
        p.region = p.provider == Provider::CloudflareR2 ? "auto" : "us-east-1";
    return p;
}

void ApplyProviderPreset(HWND dlg)
{
    int prov = static_cast<int>(::SendDlgItemMessageW(dlg, IDC_CB_PROVIDER, CB_GETCURSEL, 0, 0));
    std::wstring endpoint = GetDlgText(dlg, IDC_ED_ENDPOINT);
    std::wstring region = GetDlgText(dlg, IDC_ED_REGION);
    if (prov == 1) // Cloudflare R2
    {
        if (endpoint.empty())
            SetDlgText(dlg, IDC_ED_ENDPOINT, L"https://<ACCOUNT_ID>.r2.cloudflarestorage.com");
        if (region.empty() || region == L"us-east-1")
            SetDlgText(dlg, IDC_ED_REGION, L"auto");
    }
    else if (prov == 0) // Amazon S3
    {
        if (endpoint.empty() || endpoint.find(L"r2.cloudflarestorage.com") != std::wstring::npos)
            SetDlgText(dlg, IDC_ED_ENDPOINT, L"https://s3.us-east-1.amazonaws.com");
        if (region.empty() || region == L"auto")
            SetDlgText(dlg, IDC_ED_REGION, L"us-east-1");
    }
}

INT_PTR CALLBACK ProfilesDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* st = reinterpret_cast<ProfilesState*>(::GetWindowLongPtrW(dlg, GWLP_USERDATA));
    auto& plugin = NppS3Plugin::Instance();

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        st = new ProfilesState();
        st->connectId = reinterpret_cast<std::string*>(lParam);
        ::SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        LocalizeProfilesDialog(dlg);

        HWND prov = ::GetDlgItem(dlg, IDC_CB_PROVIDER);
        ::SendMessageW(prov, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Amazon S3"));
        ::SendMessageW(prov, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cloudflare R2"));
        ::SendMessageW(prov, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom S3 Compatible"));

        FillProfileList(dlg, plugin.Profiles().Settings().activeProfileId);
        LoadProfileFields(dlg, st);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_PROFILE_LIST:
            if (HIWORD(wParam) == LBN_SELCHANGE && st)
            {
                st->creatingNew = false;
                LoadProfileFields(dlg, st);
            }
            else if (HIWORD(wParam) == LBN_DBLCLK && st && !st->editingId.empty())
            {
                // Double-click activates the profile: close and connect.
                plugin.Profiles().Settings().activeProfileId = st->editingId;
                plugin.Profiles().Save();
                if (st->connectId)
                    *st->connectId = st->editingId;
                st->connectRequested = true;
                ::EndDialog(dlg, IDOK);
            }
            return TRUE;

        case IDC_CB_PROVIDER:
            if (HIWORD(wParam) == CBN_SELCHANGE)
                ApplyProviderPreset(dlg);
            return TRUE;

        case IDC_BTN_NEW:
            if (st)
            {
                st->creatingNew = true;
                st->editingId.clear();
                ::SendDlgItemMessageW(dlg, IDC_PROFILE_LIST, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
                LoadProfileFields(dlg, st);
                ::SendDlgItemMessageW(dlg, IDC_CB_PROVIDER, CB_SETCURSEL, 1, 0); // R2 default
                ApplyProviderPreset(dlg);
                ::CheckDlgButton(dlg, IDC_CHK_PATHSTYLE, BST_CHECKED);
                ::CheckDlgButton(dlg, IDC_CHK_AUTOUPLOAD, BST_CHECKED);
                ::SetFocus(::GetDlgItem(dlg, IDC_ED_NAME));
            }
            return TRUE;

        case IDC_BTN_DUPLICATE:
            if (st && !st->editingId.empty())
            {
                const Profile* src = plugin.Profiles().FindById(st->editingId);
                if (!src)
                    return TRUE;
                Profile copy = *src;
                copy.id = ProfileManager::GenerateId();
                copy.name = src->name + WideToUtf8(T(StrId::ProfileCopySuffix));

                // Carry the credential across so the duplicate is usable at once.
                if (auto secret = plugin.Profiles().GetSecret(src->id))
                {
                    plugin.Profiles().SetSecret(copy.id, *secret);
                    ::SecureZeroMemory(secret->data(), secret->size());
                }
                plugin.Profiles().AddOrUpdate(copy);
                plugin.Profiles().Save();

                st->creatingNew = false;
                FillProfileList(dlg, copy.id);
                LoadProfileFields(dlg, st);
                ::SetFocus(::GetDlgItem(dlg, IDC_ED_NAME));
                ::SendDlgItemMessageW(dlg, IDC_ED_NAME, EM_SETSEL, 0, -1);
            }
            return TRUE;

        case IDC_BTN_DELETE:
            if (st && !st->editingId.empty())
            {
                const Profile* p = plugin.Profiles().FindById(st->editingId);
                if (!p)
                    return TRUE;
                wchar_t confirm[512];
                ::_snwprintf_s(confirm, _TRUNCATE, T(StrId::MsgConfirmDeleteProfile),
                               Utf8ToWide(p->name).c_str());
                if (::MessageBoxW(dlg, confirm, T(StrId::DlgProfilesTitle),
                                  MB_YESNO | MB_ICONWARNING) == IDYES)
                {
                    plugin.Profiles().Remove(st->editingId);
                    plugin.Profiles().Save();
                    st->creatingNew = false;
                    FillProfileList(dlg, "");
                    LoadProfileFields(dlg, st);
                }
            }
            return TRUE;

        case IDC_BTN_SAVE:
            if (st)
            {
                Profile p = GatherProfileFields(dlg, st);
                if (p.name.empty() || p.endpoint.empty() || p.accessKeyId.empty())
                {
                    ::MessageBoxW(dlg, T(StrId::MsgFieldsRequired), T(StrId::DlgProfilesTitle),
                                  MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                // The field always shows the stored secret, so an empty field
                // now means the user cleared it rather than "keep existing".
                std::wstring wideSecret = GetDlgText(dlg, IDC_ED_SECRET);
                std::string secret = WideToUtf8(wideSecret);
                if (!wideSecret.empty())
                    ::SecureZeroMemory(wideSecret.data(), wideSecret.size() * sizeof(wchar_t));
                if (secret.empty())
                {
                    ::MessageBoxW(dlg, T(StrId::MsgSecretRequired), T(StrId::DlgProfilesTitle),
                                  MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                if (p.id.empty())
                    p.id = ProfileManager::GenerateId();
                plugin.Profiles().SetSecret(p.id, secret);
                ::SecureZeroMemory(secret.data(), secret.size());

                plugin.Profiles().AddOrUpdate(p);
                if (plugin.Profiles().Settings().activeProfileId.empty())
                    plugin.Profiles().Settings().activeProfileId = p.id;
                plugin.Profiles().Save();

                st->creatingNew = false;
                st->editingId = p.id;
                FillProfileList(dlg, p.id);
                ::MessageBoxW(dlg, T(StrId::MsgProfileSaved), T(StrId::DlgProfilesTitle),
                              MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;

        case IDC_BTN_TEST:
            if (st && !st->testRunning)
            {
                Profile p = GatherProfileFields(dlg, st);
                if (p.endpoint.empty() || p.accessKeyId.empty())
                {
                    ::MessageBoxW(dlg, T(StrId::MsgFieldsRequired), T(StrId::DlgProfilesTitle),
                                  MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                std::string secret = WideToUtf8(GetDlgText(dlg, IDC_ED_SECRET));
                st->testRunning = true;
                ::EnableWindow(::GetDlgItem(dlg, IDC_BTN_TEST), FALSE);
                ::EnableWindow(::GetDlgItem(dlg, IDCANCEL), FALSE);

                // Network runs off the UI thread; result marshalled back.
                std::thread([dlg, p, secret]() mutable {
                    VoidResult r = NppS3Plugin::Instance().TestProfileConnection(p, secret);
                    ::SecureZeroMemory(secret.data(), secret.size());
                    auto* detail = new std::wstring(
                        r.ok ? L"" : Utf8ToWide(r.error.Describe()));
                    ::PostMessageW(dlg, WM_TESTRESULT, r.ok ? 1 : 0,
                                   reinterpret_cast<LPARAM>(detail));
                }).detach();
            }
            return TRUE;

        case IDCANCEL:
            if (st && st->testRunning)
                return TRUE; // block close while the test thread may post back
            ::EndDialog(dlg, 0);
            return TRUE;
        }
        return FALSE;

    case WM_TESTRESULT:
    {
        auto* detail = reinterpret_cast<std::wstring*>(lParam);
        if (st)
        {
            st->testRunning = false;
            ::EnableWindow(::GetDlgItem(dlg, IDC_BTN_TEST), TRUE);
            ::EnableWindow(::GetDlgItem(dlg, IDCANCEL), TRUE);
        }
        if (wParam)
        {
            ::MessageBoxW(dlg, T(StrId::MsgTestOk), T(StrId::DlgProfilesTitle),
                          MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            wchar_t failMsg[1024];
            ::_snwprintf_s(failMsg, _TRUNCATE, T(StrId::MsgTestFail),
                           detail ? detail->c_str() : L"?");
            ::MessageBoxW(dlg, failMsg, T(StrId::DlgProfilesTitle), MB_OK | MB_ICONERROR);
        }
        delete detail;
        return TRUE;
    }

    case WM_DESTROY:
        delete st;
        ::SetWindowLongPtrW(dlg, GWLP_USERDATA, 0);
        return FALSE;
    }
    return FALSE;
}

// ------------------------------------------------------------- settings dialog

const Lang kLangOrder[] = {Lang::Auto, Lang::EN, Lang::KO, Lang::JA, Lang::ZH, Lang::RU};
const wchar_t* const kLangNames[] = {L"Auto", L"English", L"한국어",
                                     L"日本語", L"中文",
                                     L"Русский"};

void LocalizeSettingsDialog(HWND dlg)
{
    ::SetWindowTextW(dlg, T(StrId::DlgSettingsTitle));
    SetDlgText(dlg, IDC_GRP_APPEARANCE, T(StrId::GrpAppearance));
    SetDlgText(dlg, IDC_LBL_LANGUAGE, T(StrId::LblLanguage));
    SetDlgText(dlg, IDC_GRP_CACHE, T(StrId::GrpCache));
    SetDlgText(dlg, IDC_LBL_STALEDAYS, T(StrId::LblStaleCacheDays));
    SetDlgText(dlg, IDC_LBL_STALEHINT, T(StrId::LblStaleCacheHint));
    SetDlgText(dlg, IDC_LBL_CACHELOC, T(StrId::LblCacheLocation));
    SetDlgText(dlg, IDC_BTN_OPENCACHE, T(StrId::BtnOpenCacheFolder));
    SetDlgText(dlg, IDC_BTN_CLEARCACHE, T(StrId::BtnClearCacheNow));
    SetDlgText(dlg, IDOK, T(StrId::BtnSave));
    SetDlgText(dlg, IDCANCEL, T(StrId::BtnClose));
}

INT_PTR CALLBACK SettingsDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM)
{
    auto& plugin = NppS3Plugin::Instance();
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        LocalizeSettingsDialog(dlg);

        HWND lang = ::GetDlgItem(dlg, IDC_CB_LANGUAGE);
        for (const wchar_t* name : kLangNames)
            ::SendMessageW(lang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        Lang cur = LangFromString(plugin.Profiles().Settings().language);
        int sel = 0;
        for (int i = 0; i < static_cast<int>(std::size(kLangOrder)); ++i)
            if (kLangOrder[i] == cur)
                sel = i;
        ::SendMessageW(lang, CB_SETCURSEL, sel, 0);

        SetDlgText(dlg, IDC_ED_STALEDAYS,
                   std::to_wstring(plugin.Profiles().Settings().staleCacheDays));
        SetDlgText(dlg, IDC_ED_CACHELOC, plugin.CacheRoot());
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_OPENCACHE:
        {
            std::wstring root = plugin.CacheRoot();
            ::SHCreateDirectoryExW(nullptr, root.c_str(), nullptr);
            ::ShellExecuteW(dlg, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        case IDC_BTN_CLEARCACHE:
        {
            if (::MessageBoxW(dlg, T(StrId::MsgConfirmClearCache), T(StrId::DlgSettingsTitle),
                              MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return TRUE;
            int removed = plugin.ClearCacheNow();
            wchar_t msg2[256];
            ::_snwprintf_s(msg2, _TRUNCATE, T(StrId::MsgCacheCleared), removed);
            ::MessageBoxW(dlg, msg2, T(StrId::DlgSettingsTitle), MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        case IDOK:
        {
            int sel = static_cast<int>(
                ::SendDlgItemMessageW(dlg, IDC_CB_LANGUAGE, CB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(std::size(kLangOrder)))
                plugin.Profiles().Settings().language = LangToString(kLangOrder[sel]);

            int days = ::_wtoi(GetDlgText(dlg, IDC_ED_STALEDAYS).c_str());
            if (days >= 1)
                plugin.Profiles().Settings().staleCacheDays = days;

            plugin.Profiles().Save();
            plugin.ApplyLanguageSetting();
            ::EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

// --------------------------------------------------------------- upload dialog

struct UploadState
{
    UploadDialogResult* out;
    std::wstring fileName;
    std::vector<std::string> profileIds;
};

void SeedUploadFields(HWND dlg, UploadState* st)
{
    auto& mgr = NppS3Plugin::Instance().Profiles();
    int sel = static_cast<int>(::SendDlgItemMessageW(dlg, IDC_CB_UPPROFILE, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(st->profileIds.size()))
        return;
    const Profile* p = mgr.FindById(st->profileIds[static_cast<size_t>(sel)]);
    if (!p)
        return;
    SetDlgText(dlg, IDC_ED_UPBUCKET, Utf8ToWide(p->defaultBucket));
    std::string key = p->defaultPrefix + WideToUtf8(st->fileName);
    SetDlgText(dlg, IDC_ED_UPKEY, Utf8ToWide(key));
    SetDlgText(dlg, IDC_ED_UPCTYPE, Utf8ToWide(MimeTypeForKey(key)));
}

INT_PTR CALLBACK UploadDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* st = reinterpret_cast<UploadState*>(::GetWindowLongPtrW(dlg, GWLP_USERDATA));
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        st = reinterpret_cast<UploadState*>(lParam);
        ::SetWindowLongPtrW(dlg, GWLP_USERDATA, lParam);
        ::SetWindowTextW(dlg, T(StrId::DlgUploadTitle));
        SetDlgText(dlg, IDC_LBL_UPPROFILE, T(StrId::LblUploadProfile));
        SetDlgText(dlg, IDC_LBL_UPBUCKET, T(StrId::LblBucket));
        SetDlgText(dlg, IDC_LBL_UPKEY, T(StrId::LblKey));
        SetDlgText(dlg, IDC_LBL_UPCTYPE, T(StrId::LblContentType));
        SetDlgText(dlg, IDOK, T(StrId::BtnOk));
        SetDlgText(dlg, IDCANCEL, T(StrId::BtnCancel));

        auto& mgr = NppS3Plugin::Instance().Profiles();
        HWND combo = ::GetDlgItem(dlg, IDC_CB_UPPROFILE);
        int select = 0;
        for (const Profile& p : mgr.Profiles())
        {
            ::SendMessageW(combo, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(Utf8ToWide(p.name).c_str()));
            if (p.id == mgr.Settings().activeProfileId)
                select = static_cast<int>(st->profileIds.size());
            st->profileIds.push_back(p.id);
        }
        ::SendMessageW(combo, CB_SETCURSEL, select, 0);
        SeedUploadFields(dlg, st);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_CB_UPPROFILE:
            if (HIWORD(wParam) == CBN_SELCHANGE && st)
                SeedUploadFields(dlg, st);
            return TRUE;
        case IDOK:
            if (st)
            {
                int sel = static_cast<int>(
                    ::SendDlgItemMessageW(dlg, IDC_CB_UPPROFILE, CB_GETCURSEL, 0, 0));
                if (sel < 0 || sel >= static_cast<int>(st->profileIds.size()))
                    return TRUE;
                st->out->profileId = st->profileIds[static_cast<size_t>(sel)];
                st->out->bucket = Trim(WideToUtf8(GetDlgText(dlg, IDC_ED_UPBUCKET)));
                st->out->key = WideToUtf8(GetDlgText(dlg, IDC_ED_UPKEY));
                st->out->contentType = Trim(WideToUtf8(GetDlgText(dlg, IDC_ED_UPCTYPE)));
                if (st->out->bucket.empty() || st->out->key.empty())
                    return TRUE;
                ::EndDialog(dlg, IDOK);
            }
            return TRUE;
        case IDCANCEL:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

// ---------------------------------------------------------------- input dialog

struct InputState
{
    const wchar_t* title;
    const wchar_t* label;
    std::wstring* value;
};

INT_PTR CALLBACK InputDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* st = reinterpret_cast<InputState*>(::GetWindowLongPtrW(dlg, GWLP_USERDATA));
    switch (msg)
    {
    case WM_INITDIALOG:
        st = reinterpret_cast<InputState*>(lParam);
        ::SetWindowLongPtrW(dlg, GWLP_USERDATA, lParam);
        ::SetWindowTextW(dlg, st->title);
        SetDlgText(dlg, IDC_LBL_INPUT, st->label);
        SetDlgText(dlg, IDC_ED_INPUT, *st->value);
        SetDlgText(dlg, IDOK, T(StrId::BtnOk));
        SetDlgText(dlg, IDCANCEL, T(StrId::BtnCancel));
        ::SendDlgItemMessageW(dlg, IDC_ED_INPUT, EM_SETSEL, 0, -1);
        ::SetFocus(::GetDlgItem(dlg, IDC_ED_INPUT));
        return FALSE; // focus already set
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            if (st)
                *st->value = GetDlgText(dlg, IDC_ED_INPUT);
            ::EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

} // namespace

bool ShowProfilesDialog(HWND parent, HINSTANCE hInstance, std::string* connectProfileId)
{
    std::string id;
    ::DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_PROFILES), parent, ProfilesDlgProc,
                      reinterpret_cast<LPARAM>(&id));
    if (id.empty())
        return false;
    if (connectProfileId)
        *connectProfileId = id;
    return true;
}

void ShowSettingsDialog(HWND parent, HINSTANCE hInstance)
{
    ::DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_SETTINGS), parent, SettingsDlgProc, 0);
}

bool ShowUploadDialog(HWND parent, HINSTANCE hInstance,
                      const std::wstring& fileName, UploadDialogResult& out)
{
    UploadState st;
    st.out = &out;
    st.fileName = fileName;
    return ::DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_UPLOAD), parent, UploadDlgProc,
                             reinterpret_cast<LPARAM>(&st)) == IDOK;
}

bool ShowInputDialog(HWND parent, HINSTANCE hInstance,
                     const wchar_t* title, const wchar_t* label, std::wstring& value)
{
    InputState st{title, label, &value};
    if (::DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_INPUT), parent, InputDlgProc,
                          reinterpret_cast<LPARAM>(&st)) != IDOK)
        return false;
    return !value.empty();
}

ConflictChoice ShowConflictDialog(HWND parent, const std::wstring& key, bool remoteGone)
{
    wchar_t body[1024];
    ::_snwprintf_s(body, _TRUNCATE,
                   remoteGone ? T(StrId::MsgRemoteGoneBody) : T(StrId::MsgConflictBody),
                   key.c_str());

    TASKDIALOG_BUTTON buttons[] = {
        {1001, T(StrId::BtnOverwrite)},
        {1002, T(StrId::BtnDownloadRemote)},
        {1003, T(StrId::BtnCancel)},
    };

    TASKDIALOGCONFIG cfg{};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = parent;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    cfg.pszWindowTitle = T(StrId::MsgConflictTitle);
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pszMainInstruction = T(StrId::MsgConflictTitle);
    cfg.pszContent = body;
    cfg.pButtons = buttons;
    cfg.cButtons = remoteGone ? 1u : 2u; // remote-gone offers only Overwrite
    // Always include Cancel as the last visible button.
    if (remoteGone)
    {
        buttons[1] = buttons[2];
        cfg.cButtons = 2;
    }
    else
    {
        cfg.cButtons = 3;
    }
    cfg.nDefaultButton = 1003;

    int pressed = 0;
    if (FAILED(::TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr)))
        return ConflictChoice::Cancel;
    if (pressed == 1001)
        return ConflictChoice::Overwrite;
    if (pressed == 1002 && !remoteGone)
        return ConflictChoice::DownloadRemote;
    return ConflictChoice::Cancel;
}

bool ShowFolderPicker(HWND parent, const wchar_t* title, std::wstring& path)
{
    // Notepad++ already initialises COM on its UI thread; both "already in
    // this mode" and "different mode" mean COM is usable without our own init.
    HRESULT init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needUninit = SUCCEEDED(init);

    bool picked = false;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&dialog))))
    {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        if (title)
            dialog->SetTitle(title);
        if (SUCCEEDED(dialog->Show(parent)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)))
            {
                PWSTR wide = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wide)) && wide)
                {
                    path = wide;
                    picked = true;
                    ::CoTaskMemFree(wide);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    if (needUninit)
        ::CoUninitialize();
    return picked;
}

} // namespace npps3
