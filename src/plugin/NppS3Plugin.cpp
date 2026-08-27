// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "NppS3Plugin.h"

#include "Version.h"
#include "../npp/Docking.h"
#include "../npp/dockingResource.h"
#include "../ui/Dialogs.h"
#include "../ui/I18n.h"
#include "../util/Mime.h"
#include "../util/StringUtil.h"

#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

namespace npps3 {
namespace {

std::wstring FormatMsg(const wchar_t* fmt, const std::wstring& arg)
{
    wchar_t buf[1024];
    ::_snwprintf_s(buf, _TRUNCATE, fmt, arg.c_str());
    return buf;
}

void CopyTextToClipboard(HWND owner, const std::wstring& text)
{
    if (!::OpenClipboard(owner))
        return;
    ::EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem)
    {
        void* p = ::GlobalLock(mem);
        if (p)
        {
            ::memcpy(p, text.c_str(), bytes);
            ::GlobalUnlock(mem);
            ::SetClipboardData(CF_UNICODETEXT, mem);
        }
        else
        {
            ::GlobalFree(mem);
        }
    }
    ::CloseClipboard();
}

// Parent prefix of "a/b/c.txt" is "a/b/"; of "a/b/" (prefix) is "a/".
std::string ParentPrefixOf(const std::string& key)
{
    std::string k = key;
    if (!k.empty() && k.back() == '/')
        k.pop_back();
    size_t slash = k.find_last_of('/');
    return slash == std::string::npos ? std::string() : k.substr(0, slash + 1);
}

std::string LeafNameOf(const std::string& key)
{
    std::string k = key;
    if (!k.empty() && k.back() == '/')
        k.pop_back();
    size_t slash = k.find_last_of('/');
    return slash == std::string::npos ? k : k.substr(slash + 1);
}

} // namespace

NppS3Plugin& NppS3Plugin::Instance()
{
    static NppS3Plugin instance;
    return instance;
}

void NppS3Plugin::OnDllAttach(HINSTANCE hModule)
{
    m_hModule = hModule;
}

std::wstring NppS3Plugin::ConfigDir() const
{
    int len = static_cast<int>(::SendMessageW(m_npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, 0, 0));
    if (len <= 0)
        return {};
    std::wstring dir(static_cast<size_t>(len) + 1, L'\0');
    ::SendMessageW(m_npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR,
                   len + 1, reinterpret_cast<LPARAM>(dir.data()));
    dir.resize(::wcslen(dir.c_str()));
    dir += L"\\NppS3";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void NppS3Plugin::OnReady()
{
    if (m_started)
        return;

    std::wstring configDir = ConfigDir();
    if (configDir.empty())
        return;

    m_profiles.SetConfigFile(configDir + L"\\NppS3.xml");
    m_profiles.Load();
    m_documents.SetStoreFile(configDir + L"\\Documents.xml");
    m_documents.Load();

    wchar_t localAppData[MAX_PATH]{};
    DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    std::wstring cacheRoot = (n > 0 && n < MAX_PATH)
        ? std::wstring(localAppData) + L"\\NppS3\\Cache"
        : configDir + L"\\Cache";
    m_cache.SetRoot(cacheRoot);

    ApplyLanguageSetting();

    // Route log lines through the panel via PostMessage (worker-safe).
    Log::Instance().SetSink([this](LogLevel level, const std::wstring& text) {
        if (m_panel.IsCreated())
        {
            auto* line = new LogLine{level, text};
            if (!::PostMessageW(m_panel.Handle(), WM_NPPS3_LOG, 0,
                                reinterpret_cast<LPARAM>(line)))
                delete line;
        }
    });

    m_transfers = std::make_unique<TransferManager>(this);
    m_transfers->Start();

    // Drop mappings whose cache file disappeared, then prune stale cache
    // files not referenced by any mapping.
    for (const RemoteDocument& d : m_documents.All())
    {
        if (::GetFileAttributesW(d.localPath.c_str()) == INVALID_FILE_ATTRIBUTES)
            m_documents.Remove(d.localPath);
    }
    m_documents.Save();
    m_cache.CleanupStale(m_profiles.Settings().staleCacheDays,
                         [this](const std::wstring& path) {
                             return m_documents.FindByLocalPath(path) != nullptr;
                         });

    m_started = true;
    LocalizeMenu();
}

void NppS3Plugin::ApplyLanguageSetting()
{
    Lang lang = LangFromString(m_profiles.Settings().language);
    if (lang == Lang::Auto)
    {
        // Follow the Notepad++ native language.
        int len = static_cast<int>(::SendMessageW(m_npp._nppHandle,
                                                  NPPM_GETNATIVELANGFILENAME, 0, 0));
        std::string file;
        if (len > 0)
        {
            file.resize(static_cast<size_t>(len) + 1);
            ::SendMessageW(m_npp._nppHandle, NPPM_GETNATIVELANGFILENAME,
                           file.size(), reinterpret_cast<LPARAM>(file.data()));
            file.resize(::strlen(file.c_str()));
        }
        SetLanguage(DetectLanguage(file));
    }
    else
    {
        SetLanguage(lang);
    }

    if (m_started)
    {
        LocalizeMenu();
        m_panel.Relocalize();
    }
}

void NppS3Plugin::LocalizeMenu()
{
    // FuncItem labels are fixed at load time; relabel the live menu items.
    HMENU pluginMenu = reinterpret_cast<HMENU>(
        ::SendMessageW(m_npp._nppHandle, NPPM_GETMENUHANDLE, NPPPLUGINMENU, 0));
    if (!pluginMenu || m_showPanelCmdId == 0)
        return;

    const struct { int offset; StrId id; } items[] = {
        {0, StrId::MenuShowPanel},
        {1, StrId::MenuUploadCurrent},
        {2, StrId::MenuProfiles},
        {3, StrId::MenuAbout},
    };
    for (const auto& it : items)
    {
        UINT cmd = static_cast<UINT>(m_showPanelCmdId + it.offset);
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STRING;
        info.dwTypeData = const_cast<wchar_t*>(T(it.id));
        ::SetMenuItemInfoW(pluginMenu, cmd, FALSE, &info);
    }
}

void NppS3Plugin::OnShutdown()
{
    if (!m_started)
        return;
    m_started = false;

    // Order matters: stop producing events, then detach sinks, then persist.
    if (m_transfers)
    {
        m_transfers->Shutdown();
        m_transfers.reset();
    }
    Log::Instance().SetSink(nullptr);
    m_profiles.Save();
    m_documents.Save();
    ::SecureZeroMemory(m_activeConfig.secretAccessKey.data(),
                       m_activeConfig.secretAccessKey.size());
    m_panel.Destroy();
}

void NppS3Plugin::OnNotification(SCNotification* n)
{
    if (!n)
        return;
    switch (n->nmhdr.code)
    {
    case NPPN_READY:
        OnReady();
        break;
    case NPPN_SHUTDOWN:
        OnShutdown();
        break;
    case NPPN_FILESAVED:
        if (m_started)
            HandleFileSaved(static_cast<uintptr_t>(n->nmhdr.idFrom));
        break;
    case NPPN_FILECLOSED:
        // Keep the mapping: reopening the cached file re-links automatically.
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------------ commands

void NppS3Plugin::CmdShowPanel()
{
    if (!m_started)
        return;

    if (!m_panel.IsCreated())
    {
        DockPanel::RegisterWindowClass(m_hModule);
        if (!m_panel.Create(m_npp._nppHandle, m_hModule))
            return;
    }

    if (!m_panelRegistered)
    {
        static std::wstring moduleName;
        wchar_t path[MAX_PATH]{};
        ::GetModuleFileNameW(m_hModule, path, MAX_PATH);
        const wchar_t* leaf = ::wcsrchr(path, L'\\');
        moduleName = leaf ? leaf + 1 : path;

        DockedWidgetData data{};
        data.hClient = m_panel.Handle();
        data.pszName = T(StrId::PanelTitle);
        data.dlgID = 0; // index of the Show-panel FuncItem
        data.uMask = DWS_DF_CONT_RIGHT;
        data.pszModuleName = moduleName.c_str();
        ::SendMessageW(m_npp._nppHandle, NPPM_DMMREGASDCKDLG, 0,
                       reinterpret_cast<LPARAM>(&data));
        m_panelRegistered = true;
        m_panel.ReloadProfiles();
        ::SendMessageW(m_npp._nppHandle, NPPM_MODELESSDIALOG, MODELESSDIALOGADD,
                       reinterpret_cast<LPARAM>(m_panel.Handle()));
    }

    if (m_panelVisible)
    {
        ::SendMessageW(m_npp._nppHandle, NPPM_DMMHIDE, 0,
                       reinterpret_cast<LPARAM>(m_panel.Handle()));
        m_panelVisible = false;
    }
    else
    {
        ::SendMessageW(m_npp._nppHandle, NPPM_DMMSHOW, 0,
                       reinterpret_cast<LPARAM>(m_panel.Handle()));
        m_panelVisible = true;
    }
    UpdateShowPanelCheck(m_panelVisible);
}

void NppS3Plugin::OnPanelClosedByUser()
{
    if (m_panelVisible)
    {
        ::SendMessageW(m_npp._nppHandle, NPPM_DMMHIDE, 0,
                       reinterpret_cast<LPARAM>(m_panel.Handle()));
        m_panelVisible = false;
        UpdateShowPanelCheck(false);
    }
}

void NppS3Plugin::UpdateShowPanelCheck(bool shown)
{
    if (m_showPanelCmdId != 0)
        ::SendMessageW(m_npp._nppHandle, NPPM_SETMENUITEMCHECK,
                       static_cast<WPARAM>(m_showPanelCmdId), shown ? TRUE : FALSE);
}

void NppS3Plugin::CmdUploadCurrent()
{
    if (!m_started)
        return;
    if (m_profiles.Profiles().empty())
    {
        ::MessageBoxW(m_npp._nppHandle, T(StrId::MsgNoProfile), T(StrId::PanelTitle),
                      MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring docPath = CurrentDocPath();
    if (docPath.empty() || ::GetFileAttributesW(docPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        ::MessageBoxW(m_npp._nppHandle, T(StrId::MsgNoDocument), T(StrId::PanelTitle),
                      MB_OK | MB_ICONINFORMATION);
        return;
    }

    const wchar_t* leaf = ::wcsrchr(docPath.c_str(), L'\\');
    std::wstring fileName = leaf ? leaf + 1 : docPath;

    UploadDialogResult res;
    if (!ShowUploadDialog(m_npp._nppHandle, m_hModule, fileName, res))
        return;

    const Profile* p = m_profiles.FindById(res.profileId);
    if (!p)
        return;
    std::string error;
    S3Config cfg = BuildConfig(*p, &error);
    if (!error.empty())
    {
        ::MessageBoxW(m_npp._nppHandle, Utf8ToWide(error).c_str(), T(StrId::MsgErrorTitle),
                      MB_OK | MB_ICONERROR);
        return;
    }

    TransferRequest req;
    req.op = TransferOp::Upload;
    req.s3 = cfg;
    req.bucket = res.bucket;
    req.key = res.key;
    req.localPath = docPath;
    req.contentType = res.contentType.empty() ? MimeTypeForKey(res.key) : res.contentType;

    PendingAction action;
    action.kind = ActionKind::UploadManual;
    action.bucket = res.bucket;
    action.key = res.key;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::CmdProfiles()
{
    if (!m_started)
        return;
    ShowProfilesDialog(m_npp._nppHandle, m_hModule);
    m_panel.ReloadProfiles();
}

void NppS3Plugin::CmdAbout()
{
    wchar_t body[512];
    ::_snwprintf_s(body, _TRUNCATE, T(StrId::AboutBody), NPPS3_VERSION_WSTR);
    ::MessageBoxW(m_npp._nppHandle, body, T(StrId::AboutTitle), MB_OK | MB_ICONINFORMATION);
}

// ----------------------------------------------------------------- connection

const Profile* NppS3Plugin::ActiveProfile() const
{
    return m_profiles.FindById(m_profiles.Settings().activeProfileId);
}

S3Config NppS3Plugin::BuildConfig(const Profile& p, std::string* error) const
{
    S3Config cfg;
    cfg.endpoint = p.endpoint;
    cfg.region = p.region.empty() ? "auto" : p.region;
    cfg.accessKeyId = p.accessKeyId;
    cfg.pathStyle = p.pathStyle;
    auto secret = m_profiles.GetSecret(p.id);
    if (!secret)
    {
        if (error)
            *error = "No stored credential for this profile. Re-enter the Secret Access Key in Profiles.";
        return cfg;
    }
    cfg.secretAccessKey = *secret;
    return cfg;
}

VoidResult NppS3Plugin::TestProfileConnection(const Profile& p, const std::string& secretOverride)
{
    S3Config cfg;
    cfg.endpoint = p.endpoint;
    cfg.region = p.region.empty() ? "auto" : p.region;
    cfg.accessKeyId = p.accessKeyId;
    cfg.pathStyle = p.pathStyle;
    if (!secretOverride.empty())
    {
        cfg.secretAccessKey = secretOverride;
    }
    else
    {
        auto secret = m_profiles.GetSecret(p.id);
        if (!secret)
        {
            StorageError e;
            e.kind = ErrorKind::InvalidCredentials;
            e.message = "No stored credential for this profile";
            return VoidResult::Failure(e);
        }
        cfg.secretAccessKey = *secret;
    }
    cfg.maxRetries = 1;
    S3Client client(cfg);
    VoidResult r = client.TestConnection(p.defaultBucket);
    ::SecureZeroMemory(cfg.secretAccessKey.data(), cfg.secretAccessKey.size());
    return r;
}

void NppS3Plugin::OnProfileSelectionChanged(const std::string& profileId)
{
    if (profileId.empty() || profileId == m_profiles.Settings().activeProfileId)
        return;
    m_profiles.Settings().activeProfileId = profileId;
    m_profiles.Save();
    m_connected = false;
    m_panel.SetConnectedUi(false);
    m_panel.ClearTree();
}

void NppS3Plugin::ConnectActiveProfile()
{
    const Profile* p = ActiveProfile();
    if (!p)
    {
        ::MessageBoxW(m_npp._nppHandle, T(StrId::MsgNoProfile), T(StrId::PanelTitle),
                      MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string error;
    S3Config cfg = BuildConfig(*p, &error);
    if (!error.empty())
    {
        ::MessageBoxW(m_npp._nppHandle, Utf8ToWide(error).c_str(), T(StrId::MsgErrorTitle),
                      MB_OK | MB_ICONERROR);
        return;
    }
    m_activeConfig = cfg;

    TransferRequest req;
    req.op = TransferOp::TestConnection;
    req.s3 = cfg;
    req.bucket = p->defaultBucket;

    PendingAction action;
    action.kind = ActionKind::ConnectTest;
    Enqueue(std::move(req), action);
}

// -------------------------------------------------------------------- listing

unsigned long long NppS3Plugin::Enqueue(TransferRequest req, PendingAction action)
{
    if (!m_transfers)
        return 0;
    unsigned long long id = m_transfers->Enqueue(std::move(req));
    if (id != 0)
        m_pending[id] = action;
    return id;
}

void NppS3Plugin::RefreshTree()
{
    if (!m_connected)
    {
        ConnectActiveProfile();
        return;
    }
    const Profile* p = ActiveProfile();
    if (!p)
        return;

    m_panel.ClearTree();
    if (p->defaultBucket.empty())
    {
        TransferRequest req;
        req.op = TransferOp::ListBuckets;
        req.s3 = m_activeConfig;
        PendingAction action;
        action.kind = ActionKind::ListBucketsRoot;
        Enqueue(std::move(req), action);
    }
    else
    {
        HTREEITEM bucketItem = m_panel.AddBucket(p->defaultBucket);
        RequestChildren(bucketItem);
    }
}

void NppS3Plugin::StartListing(HTREEITEM item, const std::string& bucket,
                               const std::string& prefix, const std::string& token, int page)
{
    TransferRequest req;
    req.op = TransferOp::List;
    req.s3 = m_activeConfig;
    req.bucket = bucket;
    req.prefix = prefix;
    req.delimiter = "/";
    req.continuationToken = token;
    req.maxKeys = 1000;

    PendingAction action;
    action.kind = ActionKind::ListNode;
    action.item = item;
    action.bucket = bucket;
    action.key = prefix;
    action.page = page;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::RequestChildren(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->loading || !m_connected)
        return;
    std::string prefix = data->kind == NodeKind::Bucket ? std::string() : data->key;
    if (data->kind == NodeKind::Bucket)
    {
        const Profile* p = ActiveProfile();
        if (p && !p->defaultPrefix.empty())
            prefix = p->defaultPrefix;
    }
    m_panel.SetNodeLoading(item, true);
    StartListing(item, data->bucket, prefix, "", 0);
}

void NppS3Plugin::RefreshNode(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->kind == NodeKind::Object)
        return;
    m_panel.RemoveChildren(item);
    RequestChildren(item);
}

void NppS3Plugin::RefreshParentOfKey(const std::string& bucket, const std::string& key)
{
    HTREEITEM bucketItem = m_panel.FindBucketNode(bucket);
    if (!bucketItem)
        return;
    std::string parent = ParentPrefixOf(key);
    HTREEITEM node = parent.empty() ? bucketItem
                                    : m_panel.FindChildPrefixNode(bucketItem, parent);
    if (node)
        RefreshNode(node);
}

// ------------------------------------------------------------- object actions

void NppS3Plugin::OpenObject(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->kind != NodeKind::Object || !m_connected)
        return;
    const Profile* p = ActiveProfile();
    if (!p)
        return;

    std::wstring localPath = m_cache.LocalPathFor(p->id, data->bucket, data->key);
    if (!CacheManager::EnsureParentDirs(localPath))
        return;

    TransferRequest req;
    req.op = TransferOp::Download;
    req.s3 = m_activeConfig;
    req.bucket = data->bucket;
    req.key = data->key;
    req.localPath = localPath;

    PendingAction action;
    action.kind = ActionKind::DownloadOpen;
    action.bucket = data->bucket;
    action.key = data->key;
    action.localPath = localPath;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::DownloadObjectAs(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->kind != NodeKind::Object || !m_connected)
        return;

    std::wstring suggested = Utf8ToWide(LeafNameOf(data->key));
    wchar_t fileBuf[MAX_PATH]{};
    ::wcsncpy_s(fileBuf, suggested.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_npp._nppHandle;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOREADONLYRETURN;
    if (!::GetSaveFileNameW(&ofn))
        return;

    TransferRequest req;
    req.op = TransferOp::Download;
    req.s3 = m_activeConfig;
    req.bucket = data->bucket;
    req.key = data->key;
    req.localPath = fileBuf;

    PendingAction action;
    action.kind = ActionKind::DownloadAs;
    action.bucket = data->bucket;
    action.key = data->key;
    action.localPath = fileBuf;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::DeleteObjectAsk(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->kind != NodeKind::Object || !m_connected)
        return;

    std::wstring msg = FormatMsg(T(StrId::MsgConfirmDeleteObject), Utf8ToWide(data->key));
    if (::MessageBoxW(m_npp._nppHandle, msg.c_str(), T(StrId::PanelTitle),
                      MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    TransferRequest req;
    req.op = TransferOp::Delete;
    req.s3 = m_activeConfig;
    req.bucket = data->bucket;
    req.key = data->key;

    PendingAction action;
    action.kind = ActionKind::DeleteOne;
    action.bucket = data->bucket;
    action.key = data->key;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::DeletePrefixAsk(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->kind != NodeKind::Prefix || !m_connected)
        return;

    std::wstring msg = FormatMsg(T(StrId::MsgConfirmDeletePrefix), Utf8ToWide(data->key));
    if (::MessageBoxW(m_npp._nppHandle, msg.c_str(), T(StrId::PanelTitle),
                      MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    // Page through the prefix (no delimiter) and delete each object.
    TransferRequest req;
    req.op = TransferOp::List;
    req.s3 = m_activeConfig;
    req.bucket = data->bucket;
    req.prefix = data->key;
    req.maxKeys = 1000;

    PendingAction action;
    action.kind = ActionKind::DeletePrefixPage;
    action.bucket = data->bucket;
    action.key = data->key;
    action.counter = std::make_shared<int>(0);
    Enqueue(std::move(req), action);
}

void NppS3Plugin::RenameObjectAsk(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->kind != NodeKind::Object || !m_connected)
        return;

    std::wstring newName = Utf8ToWide(LeafNameOf(data->key));
    if (!ShowInputDialog(m_npp._nppHandle, m_hModule, T(StrId::DlgRenameTitle),
                         T(StrId::LblName), newName))
        return;

    std::string dstKey = ParentPrefixOf(data->key) + WideToUtf8(newName);
    if (dstKey == data->key)
        return;

    // Copy first; the source is deleted only after the copy succeeds.
    TransferRequest req;
    req.op = TransferOp::Copy;
    req.s3 = m_activeConfig;
    req.bucket = data->bucket;
    req.key = data->key;
    req.dstBucket = data->bucket;
    req.dstKey = dstKey;

    PendingAction action;
    action.kind = ActionKind::RenameCopy;
    action.bucket = data->bucket;
    action.key = data->key;
    action.dstKey = dstKey;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::NewFileAsk(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->kind == NodeKind::Object || !m_connected)
        return;

    std::wstring name;
    if (!ShowInputDialog(m_npp._nppHandle, m_hModule, T(StrId::DlgNewFileTitle),
                         T(StrId::LblName), name))
        return;

    std::string base = data->kind == NodeKind::Bucket ? std::string() : data->key;
    std::string key = base + WideToUtf8(name);

    TransferRequest req;
    req.op = TransferOp::UploadBytes;
    req.s3 = m_activeConfig;
    req.bucket = data->bucket;
    req.key = key;
    req.data.clear(); // zero-byte object
    req.contentType = MimeTypeForKey(key);

    PendingAction action;
    action.kind = ActionKind::CreateObject;
    action.bucket = data->bucket;
    action.key = key;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::NewFolderAsk(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->kind == NodeKind::Object || !m_connected)
        return;

    std::wstring name;
    if (!ShowInputDialog(m_npp._nppHandle, m_hModule, T(StrId::DlgNewFolderTitle),
                         T(StrId::LblName), name))
        return;

    // Folder = zero-byte marker object with a trailing slash. The browser
    // itself relies on delimiter listing, not on markers.
    std::string base = data->kind == NodeKind::Bucket ? std::string() : data->key;
    std::string key = base + WideToUtf8(name);
    if (key.empty() || key.back() != '/')
        key += '/';

    TransferRequest req;
    req.op = TransferOp::UploadBytes;
    req.s3 = m_activeConfig;
    req.bucket = data->bucket;
    req.key = key;
    req.data.clear();
    req.contentType = "application/x-directory";

    PendingAction action;
    action.kind = ActionKind::CreateObject;
    action.bucket = data->bucket;
    action.key = key;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::UploadFileHereAsk(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->kind == NodeKind::Object || !m_connected)
        return;

    wchar_t fileBuf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_npp._nppHandle;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!::GetOpenFileNameW(&ofn))
        return;

    const wchar_t* leaf = ::wcsrchr(fileBuf, L'\\');
    std::string name = WideToUtf8(leaf ? leaf + 1 : fileBuf);
    std::string base = data->kind == NodeKind::Bucket ? std::string() : data->key;
    std::string key = base + name;

    TransferRequest req;
    req.op = TransferOp::Upload;
    req.s3 = m_activeConfig;
    req.bucket = data->bucket;
    req.key = key;
    req.localPath = fileBuf;
    req.contentType = MimeTypeForKey(key);

    PendingAction action;
    action.kind = ActionKind::UploadManual;
    action.bucket = data->bucket;
    action.key = key;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::CopyKeyToClipboard(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data)
        return;
    CopyTextToClipboard(m_npp._nppHandle, Utf8ToWide(data->key));
    Log::Instance().Info(T(StrId::MsgKeyCopied));
}

void NppS3Plugin::CopyUriToClipboard(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data)
        return;
    std::string uri = "s3://" + data->bucket + "/" + data->key;
    CopyTextToClipboard(m_npp._nppHandle, Utf8ToWide(uri));
    Log::Instance().Info(T(StrId::MsgKeyCopied));
}

void NppS3Plugin::ShowProperties(HTREEITEM item)
{
    NodeData* data = m_panel.DataOf(item);
    if (!data || data->kind != NodeKind::Object || !m_connected)
        return;

    TransferRequest req;
    req.op = TransferOp::Head;
    req.s3 = m_activeConfig;
    req.bucket = data->bucket;
    req.key = data->key;

    PendingAction action;
    action.kind = ActionKind::PropertiesHead;
    action.bucket = data->bucket;
    action.key = data->key;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::CancelTransfer(unsigned long long id)
{
    if (m_transfers)
        m_transfers->Cancel(id);
}

// -------------------------------------------------------------- save handling

std::wstring NppS3Plugin::PathFromBufferId(uintptr_t bufferId) const
{
    LRESULT len = ::SendMessageW(m_npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID,
                                 static_cast<WPARAM>(bufferId), 0);
    if (len <= 0)
        return {};
    std::wstring path(static_cast<size_t>(len) + 1, L'\0');
    ::SendMessageW(m_npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID,
                   static_cast<WPARAM>(bufferId), reinterpret_cast<LPARAM>(path.data()));
    path.resize(::wcslen(path.c_str()));
    return path;
}

std::wstring NppS3Plugin::CurrentDocPath() const
{
    LRESULT bufferId = ::SendMessageW(m_npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
    if (!bufferId)
        return {};
    return PathFromBufferId(static_cast<uintptr_t>(bufferId));
}

void NppS3Plugin::HandleFileSaved(uintptr_t bufferId)
{
    std::wstring path = PathFromBufferId(bufferId);
    if (path.empty())
        return;
    const RemoteDocument* doc = m_documents.FindByLocalPath(path);
    if (!doc || !doc->autoUpload)
        return; // ordinary local file — never uploaded

    std::wstring norm = RemoteDocumentManager::NormalizePath(path);
    SaveState& state = m_saveStates[norm];
    if (state.inFlight)
    {
        // A save arrived while the previous upload is still running; rerun
        // once it completes so the final content always wins.
        state.again = true;
        return;
    }
    state.inFlight = true;
    state.again = false;
    StartSaveUpload(path);
}

void NppS3Plugin::StartSaveUpload(const std::wstring& localPath)
{
    const RemoteDocument* doc = m_documents.FindByLocalPath(localPath);
    if (!doc)
    {
        FinishSave(localPath, false);
        return;
    }

    const Profile* p = m_profiles.FindById(doc->profileId);
    if (!p)
    {
        FinishSave(localPath, false);
        return;
    }
    std::string error;
    S3Config cfg = BuildConfig(*p, &error);
    if (!error.empty())
    {
        Log::Instance().Error(Utf8ToWide(error));
        FinishSave(localPath, false);
        return;
    }

    // Conflict detection: compare the remote ETag before overwriting.
    TransferRequest req;
    req.op = TransferOp::Head;
    req.s3 = cfg;
    req.bucket = doc->bucket;
    req.key = doc->key;

    PendingAction action;
    action.kind = ActionKind::HeadForSave;
    action.bucket = doc->bucket;
    action.key = doc->key;
    action.localPath = localPath;
    Enqueue(std::move(req), action);
}

void NppS3Plugin::FinishSave(const std::wstring& localPath, bool restartIfDirty)
{
    std::wstring norm = RemoteDocumentManager::NormalizePath(localPath);
    auto it = m_saveStates.find(norm);
    if (it == m_saveStates.end())
        return;
    bool again = it->second.again;
    it->second.inFlight = false;
    it->second.again = false;
    if (again && restartIfDirty)
    {
        it->second.inFlight = true;
        StartSaveUpload(localPath);
    }
}

// ------------------------------------------------------- transfer completions

void NppS3Plugin::OnTransferEvent(const TransferEvent& ev)
{
    // Worker thread: marshal to the panel window's UI thread.
    if (!m_panel.IsCreated())
        return;
    auto* copy = new TransferEvent(ev);
    if (!::PostMessageW(m_panel.Handle(), WM_NPPS3_TRANSFER, 0,
                        reinterpret_cast<LPARAM>(copy)))
        delete copy;
}

void NppS3Plugin::HandleTransferEventUi(TransferEvent* evPtr)
{
    std::unique_ptr<TransferEvent> ev(evPtr);
    if (!ev)
        return;

    m_panel.UpsertTransfer(*ev);
    if (ev->type != TransferEvent::Type::Finished)
        return;

    auto pendingIt = m_pending.find(ev->id);
    PendingAction action;
    if (pendingIt != m_pending.end())
    {
        action = pendingIt->second;
        m_pending.erase(pendingIt);
    }

    const bool ok = ev->state == TransferState::Completed;
    const bool cancelled = ev->state == TransferState::Cancelled;

    switch (action.kind)
    {
    case ActionKind::ConnectTest:
    {
        const Profile* p = ActiveProfile();
        std::wstring name = p ? Utf8ToWide(p->name) : L"?";
        if (ok)
        {
            m_connected = true;
            m_panel.SetConnectedUi(true);
            Log::Instance().Info(FormatMsg(T(StrId::MsgConnected), name));
            RefreshTree();
        }
        else if (!cancelled)
        {
            m_connected = false;
            ShowError(ev->error, FormatMsg(T(StrId::MsgConnectFailed),
                                           Utf8ToWide(ev->error.Describe())));
        }
        break;
    }

    case ActionKind::ListBucketsRoot:
        if (ok && ev->result)
        {
            for (const BucketInfo& b : ev->result->buckets)
                m_panel.AddBucket(b.name);
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::ListNode:
        if (ok && ev->result)
        {
            m_panel.AppendListing(action.item, action.key, ev->result->listing);
            if (ev->result->listing.isTruncated &&
                !ev->result->listing.nextContinuationToken.empty() && action.page < 100)
            {
                StartListing(action.item, action.bucket, action.key,
                             ev->result->listing.nextContinuationToken, action.page + 1);
            }
            else
            {
                m_panel.FinishNode(action.item);
            }
        }
        else
        {
            m_panel.SetNodeLoading(action.item, false);
            if (!cancelled)
                ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::DownloadOpen:
        if (ok && ev->result)
        {
            const Profile* p = ActiveProfile();
            RemoteDocument doc;
            doc.profileId = p ? p->id : "";
            doc.bucket = action.bucket;
            doc.key = action.key;
            doc.localPath = action.localPath;
            doc.etag = ev->result->metadata.etag;
            doc.lastModified = ev->result->metadata.lastModified;
            doc.autoUpload = p ? p->autoUploadOnSave : true;
            m_documents.Register(doc);
            m_documents.Save();

            Log::Instance().Info(FormatMsg(T(StrId::MsgDownloadDone), Utf8ToWide(action.key)));
            ::SendMessageW(m_npp._nppHandle, NPPM_DOOPEN, 0,
                           reinterpret_cast<LPARAM>(action.localPath.c_str()));
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::DownloadAs:
        if (ok)
            Log::Instance().Info(FormatMsg(T(StrId::MsgDownloadDone), Utf8ToWide(action.key)));
        else if (!cancelled)
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        break;

    case ActionKind::HeadForSave:
    {
        RemoteDocument* doc = m_documents.FindByLocalPath(action.localPath);
        if (!doc)
        {
            FinishSave(action.localPath, false);
            break;
        }

        bool proceed = false;
        if (ok && ev->result)
        {
            const std::string& remoteEtag = ev->result->metadata.etag;
            if (!doc->etag.empty() && !remoteEtag.empty() && remoteEtag != doc->etag)
            {
                // Remote changed since download.
                ConflictChoice choice = ShowConflictDialog(
                    m_npp._nppHandle, Utf8ToWide(doc->key), false);
                if (choice == ConflictChoice::Overwrite)
                {
                    proceed = true;
                }
                else if (choice == ConflictChoice::DownloadRemote)
                {
                    TransferRequest req;
                    req.op = TransferOp::Download;
                    req.s3 = m_activeConfig.secretAccessKey.empty() ? req.s3 : m_activeConfig;
                    // Rebuild config from the document's profile to be safe.
                    if (const Profile* p = m_profiles.FindById(doc->profileId))
                    {
                        std::string error;
                        req.s3 = BuildConfig(*p, &error);
                    }
                    req.bucket = doc->bucket;
                    req.key = doc->key;
                    req.localPath = doc->localPath;

                    PendingAction a2;
                    a2.kind = ActionKind::SaveConflictDownload;
                    a2.bucket = doc->bucket;
                    a2.key = doc->key;
                    a2.localPath = doc->localPath;
                    Enqueue(std::move(req), a2);
                    FinishSave(action.localPath, false);
                    break;
                }
            }
            else
            {
                proceed = true;
            }
        }
        else if (!ok && ev->error.kind == ErrorKind::NoSuchKey)
        {
            // Remote object vanished; ask before re-creating it.
            ConflictChoice choice = ShowConflictDialog(
                m_npp._nppHandle, Utf8ToWide(doc->key), true);
            proceed = choice == ConflictChoice::Overwrite;
        }
        else if (!ok)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }

        if (proceed)
        {
            const Profile* p = m_profiles.FindById(doc->profileId);
            std::string error;
            S3Config cfg = p ? BuildConfig(*p, &error) : S3Config{};
            if (!p || !error.empty())
            {
                if (!error.empty())
                    Log::Instance().Error(Utf8ToWide(error));
                FinishSave(action.localPath, false);
                break;
            }
            TransferRequest req;
            req.op = TransferOp::Upload;
            req.s3 = cfg;
            req.bucket = doc->bucket;
            req.key = doc->key;
            req.localPath = doc->localPath;
            req.contentType = MimeTypeForKey(doc->key);

            PendingAction a2;
            a2.kind = ActionKind::UploadSave;
            a2.bucket = doc->bucket;
            a2.key = doc->key;
            a2.localPath = doc->localPath;
            Enqueue(std::move(req), a2);
        }
        else
        {
            FinishSave(action.localPath, false);
        }
        break;
    }

    case ActionKind::UploadSave:
        if (ok && ev->result)
        {
            m_documents.UpdateRemoteState(action.localPath, ev->result->put.etag, "");
            m_documents.Save();
            Log::Instance().Info(FormatMsg(T(StrId::MsgUploadDone), Utf8ToWide(action.key)));
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        FinishSave(action.localPath, true);
        break;

    case ActionKind::SaveConflictDownload:
        if (ok && ev->result)
        {
            m_documents.UpdateRemoteState(action.localPath, ev->result->metadata.etag,
                                          ev->result->metadata.lastModified);
            m_documents.Save();
            // Reload the buffer so the editor shows the fresh remote content.
            ::SendMessageW(m_npp._nppHandle, NPPM_DOOPEN, 0,
                           reinterpret_cast<LPARAM>(action.localPath.c_str()));
            LRESULT bufferId = ::SendMessageW(m_npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
            if (bufferId)
                ::SendMessageW(m_npp._nppHandle, NPPM_RELOADBUFFERID,
                               static_cast<WPARAM>(bufferId), FALSE);
            Log::Instance().Info(FormatMsg(T(StrId::MsgDownloadDone), Utf8ToWide(action.key)));
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::UploadManual:
        if (ok)
        {
            Log::Instance().Info(FormatMsg(T(StrId::MsgUploadDone), Utf8ToWide(action.key)));
            RefreshParentOfKey(action.bucket, action.key);
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::CreateObject:
        if (ok)
        {
            Log::Instance().Info(FormatMsg(T(StrId::MsgUploadDone), Utf8ToWide(action.key)));
            RefreshParentOfKey(action.bucket, action.key);
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::RenameCopy:
        if (ok)
        {
            // Copy verified — now delete the original.
            TransferRequest req;
            req.op = TransferOp::Delete;
            req.s3 = m_activeConfig;
            req.bucket = action.bucket;
            req.key = action.key;

            PendingAction a2;
            a2.kind = ActionKind::RenameDelete;
            a2.bucket = action.bucket;
            a2.key = action.key;
            a2.dstKey = action.dstKey;
            Enqueue(std::move(req), a2);
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::RenameDelete:
        if (ok)
        {
            Log::Instance().Info(FormatMsg(T(StrId::MsgRenamed), Utf8ToWide(action.dstKey)));
            RefreshParentOfKey(action.bucket, action.dstKey);
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::DeleteOne:
        if (ok)
        {
            Log::Instance().Info(FormatMsg(T(StrId::MsgDeleted), Utf8ToWide(action.key)));
            RefreshParentOfKey(action.bucket, action.key);
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::DeletePrefixPage:
        if (ok && ev->result)
        {
            for (const ObjectInfo& obj : ev->result->listing.objects)
            {
                TransferRequest req;
                req.op = TransferOp::Delete;
                req.s3 = m_activeConfig;
                req.bucket = action.bucket;
                req.key = obj.key;

                PendingAction a2;
                a2.kind = ActionKind::DeletePrefixItem;
                a2.bucket = action.bucket;
                a2.key = obj.key;
                a2.dstKey = action.key; // remembers the prefix for the refresh
                Enqueue(std::move(req), a2);
            }
            if (ev->result->listing.isTruncated &&
                !ev->result->listing.nextContinuationToken.empty())
            {
                TransferRequest req;
                req.op = TransferOp::List;
                req.s3 = m_activeConfig;
                req.bucket = action.bucket;
                req.prefix = action.key;
                req.continuationToken = ev->result->listing.nextContinuationToken;
                req.maxKeys = 1000;
                Enqueue(std::move(req), action);
            }
            else
            {
                Log::Instance().Info(FormatMsg(T(StrId::MsgDeleted), Utf8ToWide(action.key)));
                RefreshParentOfKey(action.bucket, action.key);
            }
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::DeletePrefixItem:
        // Individual deletions log only on failure to avoid log spam.
        if (!ok && !cancelled)
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        break;

    case ActionKind::PropertiesHead:
        if (ok && ev->result)
        {
            const ObjectMetadata& md = ev->result->metadata;
            std::wstring body = Utf8ToWide(action.key) + L"\n\n";
            wchar_t line[256];
            ::_snwprintf_s(line, _TRUNCATE, L"%s: %llu\n", T(StrId::PropSize), md.size);
            body += line;
            body += std::wstring(T(StrId::PropLastModified)) + L": " +
                    Utf8ToWide(md.lastModified) + L"\n";
            body += std::wstring(T(StrId::PropETag)) + L": " + Utf8ToWide(md.etag) + L"\n";
            body += std::wstring(T(StrId::PropContentType)) + L": " +
                    Utf8ToWide(md.contentType) + L"\n";
            if (!md.storageClass.empty())
                body += std::wstring(T(StrId::PropStorageClass)) + L": " +
                        Utf8ToWide(md.storageClass) + L"\n";
            if (!md.versionId.empty())
                body += std::wstring(T(StrId::PropVersionId)) + L": " +
                        Utf8ToWide(md.versionId) + L"\n";
            ::MessageBoxW(m_npp._nppHandle, body.c_str(), T(StrId::PropTitle),
                          MB_OK | MB_ICONINFORMATION);
        }
        else if (!cancelled)
        {
            ShowError(ev->error, Utf8ToWide(ev->error.Describe()));
        }
        break;

    case ActionKind::None:
    default:
        break;
    }
}

void NppS3Plugin::ShowError(const StorageError& e, const std::wstring& contextMsg)
{
    // Errors surface in the log; only credential problems interrupt the user.
    Log::Instance().Error(contextMsg);
    if (e.kind == ErrorKind::InvalidCredentials || e.kind == ErrorKind::AccessDenied)
        ::MessageBoxW(m_npp._nppHandle, contextMsg.c_str(), T(StrId::MsgErrorTitle),
                      MB_OK | MB_ICONERROR);
}

} // namespace npps3
