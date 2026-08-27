// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DockPanel.h"

#include "I18n.h"
#include "Icons.h"
#include "../npp/dockingResource.h"
#include "../plugin/NppS3Plugin.h"
#include "../util/StringUtil.h"

#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

namespace npps3 {
namespace {

const wchar_t* kPanelClass = L"NppS3PanelClass";

// Child control ids
constexpr int IDC_PROFILE = 3001;
constexpr int IDC_CONNECT = 3002;
constexpr int IDC_REFRESH = 3003;
constexpr int IDC_UPLOAD = 3004;
constexpr int IDC_PROFILESBTN = 3005;
constexpr int IDC_TREE = 3006;
constexpr int IDC_TRANSFERS = 3007;
constexpr int IDC_LOG = 3008;

// Context menu command ids (panel-local)
constexpr int IDM_CTX_OPEN = 3100;
constexpr int IDM_CTX_DOWNLOAD_AS = 3101;
constexpr int IDM_CTX_RENAME = 3102;
constexpr int IDM_CTX_COPY_KEY = 3103;
constexpr int IDM_CTX_COPY_URI = 3104;
constexpr int IDM_CTX_DELETE = 3105;
constexpr int IDM_CTX_PROPERTIES = 3106;
constexpr int IDM_CTX_NEW_FILE = 3107;
constexpr int IDM_CTX_NEW_FOLDER = 3108;
constexpr int IDM_CTX_UPLOAD_HERE = 3109;
constexpr int IDM_CTX_REFRESH = 3110;
constexpr int IDM_CTX_CANCEL = 3111;
constexpr int IDM_CTX_CLEAR_FINISHED = 3112;
constexpr int IDM_GEAR_PROFILES = 3113;
constexpr int IDM_GEAR_SETTINGS = 3114;

std::wstring FormatBytes(uint64_t bytes)
{
    wchar_t buf[64];
    if (bytes >= 1024ull * 1024 * 1024)
        ::_snwprintf_s(buf, _TRUNCATE, L"%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ull * 1024)
        ::_snwprintf_s(buf, _TRUNCATE, L"%.1f MB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        ::_snwprintf_s(buf, _TRUNCATE, L"%.1f KB", bytes / 1024.0);
    else
        ::_snwprintf_s(buf, _TRUNCATE, L"%llu B", bytes);
    return buf;
}

const wchar_t* LocalizedState(TransferState st)
{
    switch (st)
    {
    case TransferState::Pending: return T(StrId::StatePending);
    case TransferState::Running: return T(StrId::StateRunning);
    case TransferState::Completed: return T(StrId::StateCompleted);
    case TransferState::Failed: return T(StrId::StateFailed);
    case TransferState::Cancelled: return T(StrId::StateCancelled);
    }
    return L"?";
}

const wchar_t* LocalizedOp(TransferOp op)
{
    switch (op)
    {
    case TransferOp::ListBuckets:
    case TransferOp::TestConnection: return T(StrId::OpConnect);
    case TransferOp::List: return T(StrId::OpList);
    case TransferOp::Head: return T(StrId::OpProperties);
    case TransferOp::Download: return T(StrId::OpDownload);
    case TransferOp::Upload: return T(StrId::OpUpload);
    case TransferOp::UploadBytes: return T(StrId::OpCreate);
    case TransferOp::Delete: return T(StrId::OpDelete);
    case TransferOp::Copy: return T(StrId::OpCopy);
    }
    return L"?";
}

} // namespace

void DockPanel::RegisterWindowClass(HINSTANCE hInstance)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProcThunk;
    wc.hInstance = hInstance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kPanelClass;
    wc.style = CS_DBLCLKS;
    ::RegisterClassW(&wc);
}

bool DockPanel::Create(HWND hNppParent, HINSTANCE hInstance)
{
    if (m_hwnd)
        return true;
    m_hwnd = ::CreateWindowExW(0, kPanelClass, T(StrId::PanelTitle),
                               WS_CHILD | WS_CLIPCHILDREN,
                               0, 0, 320, 500, hNppParent, nullptr, hInstance, this);
    return m_hwnd != nullptr;
}

void DockPanel::Destroy()
{
    if (m_hwnd)
    {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_font)
    {
        ::DeleteObject(m_font);
        m_font = nullptr;
    }
}

LRESULT CALLBACK DockPanel::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DockPanel* self;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<DockPanel*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<DockPanel*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self)
        return self->WndProc(hwnd, msg, wParam, lParam);
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT DockPanel::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;

    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_COMMAND:
        OnCommand(wParam);
        return 0;

    case WM_NOTIFY:
        return OnNotify(reinterpret_cast<NMHDR*>(lParam));

    case WM_CONTEXTMENU:
    {
        HWND target = reinterpret_cast<HWND>(wParam);
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (target == m_tree)
            OnTreeContextMenu(x, y);
        else if (target == m_transfers)
            OnTransfersContextMenu(x, y);
        return 0;
    }

    case WM_NPPS3_TRANSFER:
        // Ownership of the event moves to the plugin handler.
        NppS3Plugin::Instance().HandleTransferEventUi(
            reinterpret_cast<TransferEvent*>(lParam));
        return 0;

    case WM_NPPS3_LOG:
    {
        auto* line = reinterpret_cast<LogLine*>(lParam);
        AppendLog(line->level, line->text);
        delete line;
        return 0;
    }

    case WM_CLOSE:
        // Docking manager close button: hide, never destroy.
        NppS3Plugin::Instance().OnPanelClosedByUser();
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

void DockPanel::OnCreate(HWND hwnd)
{
    HINSTANCE inst = NppS3Plugin::Instance().ModuleHandle();

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    ::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    m_font = ::CreateFontIndirectW(&ncm.lfMessageFont);

    m_profileCombo = ::CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 100, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROFILE)), inst, nullptr);

    // Icon buttons keep their label as tooltip-style text via BS_ICON + title.
    auto makeButton = [&](int id, const wchar_t* text, HICON icon) {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
        if (icon)
            style |= BS_ICON;
        HWND btn = ::CreateWindowExW(0, WC_BUTTONW, text, style,
            0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        if (icon)
            ::SendMessageW(btn, BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(icon));
        return btn;
    };
    m_btnConnect = makeButton(IDC_CONNECT, T(StrId::BtnConnect), Icons::Connect());
    m_btnRefresh = makeButton(IDC_REFRESH, T(StrId::BtnRefresh), Icons::Refresh());
    m_btnUpload = makeButton(IDC_UPLOAD, T(StrId::BtnUpload), Icons::Upload());
    m_btnProfiles = makeButton(IDC_PROFILESBTN, T(StrId::BtnProfiles), Icons::Settings());

    m_tooltip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd, nullptr, inst, nullptr);

    m_tree = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TREE)), inst, nullptr);
    if (HIMAGELIST images = Icons::TreeImageList())
        TreeView_SetImageList(m_tree, images, TVSIL_NORMAL);

    m_transfers = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER,
        0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TRANSFERS)), inst, nullptr);
    ListView_SetExtendedListViewStyle(m_transfers, LVS_EX_FULLROWSELECT);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    const struct { StrId id; int width; } cols[] = {
        {StrId::ColOperation, 70},
        {StrId::ColObject, 130},
        {StrId::ColProgress, 70},
        {StrId::ColStatus, 80},
    };
    for (int i = 0; i < 4; ++i)
    {
        col.pszText = const_cast<wchar_t*>(T(cols[i].id));
        col.cx = cols[i].width;
        ListView_InsertColumn(m_transfers, i, &col);
    }

    m_log = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG)), inst, nullptr);

    for (HWND ctl : {m_profileCombo, m_btnConnect, m_btnRefresh, m_btnUpload,
                     m_btnProfiles, m_tree, m_transfers, m_log})
        ::SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    UpdateTooltips();
    ReloadProfiles();
}

void DockPanel::OnSize(int width, int height)
{
    if (!m_profileCombo)
        return;
    const int pad = 3;
    const int rowH = 24;
    int y = pad;

    ::MoveWindow(m_profileCombo, pad, y, width - 2 * pad, 200, TRUE);
    y += rowH + pad;

    int btnW = (width - 5 * pad) / 4;
    ::MoveWindow(m_btnConnect, pad, y, btnW, rowH, TRUE);
    ::MoveWindow(m_btnRefresh, 2 * pad + btnW, y, btnW, rowH, TRUE);
    ::MoveWindow(m_btnUpload, 3 * pad + 2 * btnW, y, btnW, rowH, TRUE);
    ::MoveWindow(m_btnProfiles, 4 * pad + 3 * btnW, y, btnW, rowH, TRUE);
    y += rowH + pad;

    int remaining = height - y - pad;
    if (remaining < 120)
        remaining = 120;
    int treeH = remaining * 55 / 100;
    int transferH = remaining * 27 / 100;
    int logH = remaining - treeH - transferH - 2 * pad;

    ::MoveWindow(m_tree, pad, y, width - 2 * pad, treeH, TRUE);
    y += treeH + pad;
    ::MoveWindow(m_transfers, pad, y, width - 2 * pad, transferH, TRUE);
    y += transferH + pad;
    ::MoveWindow(m_log, pad, y, width - 2 * pad, logH, TRUE);
}

void DockPanel::OnCommand(WPARAM wParam)
{
    auto& plugin = NppS3Plugin::Instance();
    switch (LOWORD(wParam))
    {
    case IDC_CONNECT:
        plugin.ConnectActiveProfile();
        break;
    case IDC_REFRESH:
        plugin.RefreshTree();
        break;
    case IDC_UPLOAD:
        plugin.CmdUploadCurrent();
        break;
    case IDC_PROFILESBTN:
        ShowGearMenu();
        break;
    case IDM_GEAR_PROFILES:
        plugin.CmdProfiles();
        break;
    case IDM_GEAR_SETTINGS:
        plugin.CmdSettings();
        break;
    case IDC_PROFILE:
        if (HIWORD(wParam) == CBN_SELCHANGE)
            plugin.OnProfileSelectionChanged(SelectedProfileId());
        break;
    case IDM_CTX_OPEN:
        plugin.OpenObject(SelectedItem());
        break;
    case IDM_CTX_DOWNLOAD_AS:
        plugin.DownloadObjectAs(SelectedItem());
        break;
    case IDM_CTX_RENAME:
        plugin.RenameObjectAsk(SelectedItem());
        break;
    case IDM_CTX_COPY_KEY:
        plugin.CopyKeyToClipboard(SelectedItem());
        break;
    case IDM_CTX_COPY_URI:
        plugin.CopyUriToClipboard(SelectedItem());
        break;
    case IDM_CTX_DELETE:
    {
        HTREEITEM item = SelectedItem();
        NodeData* data = DataOf(item);
        if (data && data->kind == NodeKind::Object)
            plugin.DeleteObjectAsk(item);
        else if (data && data->kind == NodeKind::Prefix)
            plugin.DeletePrefixAsk(item);
        break;
    }
    case IDM_CTX_PROPERTIES:
        plugin.ShowProperties(SelectedItem());
        break;
    case IDM_CTX_NEW_FILE:
        plugin.NewFileAsk(SelectedItem());
        break;
    case IDM_CTX_NEW_FOLDER:
        plugin.NewFolderAsk(SelectedItem());
        break;
    case IDM_CTX_UPLOAD_HERE:
        plugin.UploadFileHereAsk(SelectedItem());
        break;
    case IDM_CTX_REFRESH:
        plugin.RefreshNode(SelectedItem());
        break;
    case IDM_CTX_CANCEL:
    {
        int row = ListView_GetNextItem(m_transfers, -1, LVNI_SELECTED);
        if (row >= 0)
        {
            LVITEMW item{};
            item.mask = LVIF_PARAM;
            item.iItem = row;
            if (ListView_GetItem(m_transfers, &item))
                plugin.CancelTransfer(static_cast<unsigned long long>(item.lParam));
        }
        break;
    }
    case IDM_CTX_CLEAR_FINISHED:
        ClearFinishedTransfers();
        break;
    }
}

LRESULT DockPanel::OnNotify(NMHDR* hdr)
{
    if (!hdr)
        return 0;

    // Docking-manager notifications arrive with hwndFrom == Notepad++.
    if (hdr->code == DMN_CLOSE)
    {
        NppS3Plugin::Instance().OnPanelClosedByUser();
        return 0;
    }

    if (hdr->hwndFrom == m_tree)
    {
        switch (hdr->code)
        {
        case TVN_ITEMEXPANDINGW:
        {
            auto* tv = reinterpret_cast<NMTREEVIEWW*>(hdr);
            if (tv->action == TVE_EXPAND)
            {
                NodeData* data = DataOf(tv->itemNew.hItem);
                const bool container = data && (data->kind == NodeKind::Bucket ||
                                                data->kind == NodeKind::Prefix);
                if (container && !data->loaded && !data->loading)
                {
                    NppS3Plugin::Instance().RequestChildren(tv->itemNew.hItem);
                    return TRUE; // block expansion until listing arrives
                }
            }
            return FALSE;
        }
        case NM_DBLCLK:
        case NM_RETURN:
        {
            HTREEITEM sel = SelectedItem();
            NodeData* data = DataOf(sel);
            if (data && data->kind == NodeKind::Object)
            {
                OnItemActivated(sel);
                return TRUE;
            }
            return FALSE;
        }
        case TVN_DELETEITEMW:
        {
            auto* tv = reinterpret_cast<NMTREEVIEWW*>(hdr);
            delete reinterpret_cast<NodeData*>(tv->itemOld.lParam);
            return 0;
        }
        }
    }
    return 0;
}

void DockPanel::OnItemActivated(HTREEITEM item)
{
    NppS3Plugin::Instance().OpenObject(item);
}

void DockPanel::OnTreeContextMenu(int x, int y)
{
    HTREEITEM item = nullptr;
    if (x == -1 && y == -1)
    {
        item = SelectedItem();
        RECT rc{};
        if (item && TreeView_GetItemRect(m_tree, item, &rc, TRUE))
        {
            POINT pt{rc.left, rc.bottom};
            ::ClientToScreen(m_tree, &pt);
            x = pt.x;
            y = pt.y;
        }
    }
    else
    {
        TVHITTESTINFO hit{};
        hit.pt = {x, y};
        ::ScreenToClient(m_tree, &hit.pt);
        item = TreeView_HitTest(m_tree, &hit);
        if (item)
            TreeView_SelectItem(m_tree, item);
    }
    if (!item)
        return;
    NodeData* data = DataOf(item);
    if (!data)
        return;

    HMENU menu = ::CreatePopupMenu();
    if (data->kind == NodeKind::Placeholder)
    {
        ::DestroyMenu(menu);
        return;
    }
    if (data->kind == NodeKind::Root)
    {
        ::AppendMenuW(menu, MF_STRING, IDC_CONNECT,
                      m_connected ? T(StrId::CtxDisconnect) : T(StrId::CtxConnect));
        if (m_connected)
            ::AppendMenuW(menu, MF_STRING, IDM_CTX_REFRESH, T(StrId::CtxRefresh));
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, IDC_PROFILESBTN, T(StrId::BtnProfiles));
        ::TrackPopupMenu(menu, TPM_RIGHTBUTTON, x, y, 0, m_hwnd, nullptr);
        ::DestroyMenu(menu);
        return;
    }
    if (data->kind == NodeKind::Object)
    {
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_OPEN, T(StrId::CtxOpen));
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_DOWNLOAD_AS, T(StrId::CtxDownloadAs));
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_RENAME, T(StrId::CtxRename));
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_COPY_KEY, T(StrId::CtxCopyKey));
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_COPY_URI, T(StrId::CtxCopyUri));
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_DELETE, T(StrId::CtxDelete));
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_PROPERTIES, T(StrId::CtxProperties));
    }
    else
    {
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_NEW_FILE, T(StrId::CtxNewFile));
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_NEW_FOLDER, T(StrId::CtxNewFolder));
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_UPLOAD_HERE, T(StrId::CtxUploadHere));
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, IDM_CTX_REFRESH, T(StrId::CtxRefresh));
        if (data->kind == NodeKind::Prefix)
        {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            ::AppendMenuW(menu, MF_STRING, IDM_CTX_DELETE, T(StrId::CtxDelete));
        }
    }
    ::TrackPopupMenu(menu, TPM_RIGHTBUTTON, x, y, 0, m_hwnd, nullptr);
    ::DestroyMenu(menu);
}

void DockPanel::OnTransfersContextMenu(int x, int y)
{
    if (x == -1 && y == -1)
    {
        POINT pt{0, 0};
        ::ClientToScreen(m_transfers, &pt);
        x = pt.x;
        y = pt.y;
    }
    HMENU menu = ::CreatePopupMenu();
    ::AppendMenuW(menu, MF_STRING, IDM_CTX_CANCEL, T(StrId::CtxCancel));
    ::AppendMenuW(menu, MF_STRING, IDM_CTX_CLEAR_FINISHED, T(StrId::CtxClearFinished));
    ::TrackPopupMenu(menu, TPM_RIGHTBUTTON, x, y, 0, m_hwnd, nullptr);
    ::DestroyMenu(menu);
}

void DockPanel::ShowGearMenu()
{
    // The gear covers two separate destinations now (per-connection profiles
    // vs. plugin-wide settings), so it drops a menu instead of guessing.
    RECT rc{};
    ::GetWindowRect(m_btnProfiles, &rc);

    HMENU menu = ::CreatePopupMenu();
    ::AppendMenuW(menu, MF_STRING, IDM_GEAR_PROFILES, T(StrId::MenuProfiles));
    ::AppendMenuW(menu, MF_STRING, IDM_GEAR_SETTINGS, T(StrId::MenuSettings));
    ::TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_LEFTBUTTON, rc.right, rc.bottom,
                     0, m_hwnd, nullptr);
    ::DestroyMenu(menu);
}

void DockPanel::UpdateTooltips()
{
    if (!m_tooltip)
        return;
    const struct { HWND ctl; StrId id; } tips[] = {
        {m_btnConnect, StrId::BtnConnect},
        {m_btnRefresh, StrId::BtnRefresh},
        {m_btnUpload, StrId::BtnUpload},
        {m_btnProfiles, StrId::BtnGear},
    };
    for (const auto& tip : tips)
    {
        TOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = m_hwnd;
        info.uId = reinterpret_cast<UINT_PTR>(tip.ctl);
        info.lpszText = const_cast<wchar_t*>(T(tip.id));
        // Adding an already-registered tool is ignored, so add-then-update
        // covers both first setup and relanguage.
        ::SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        ::SendMessageW(m_tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
    }
}

// ------------------------------------------------------------------- tree API

void DockPanel::ClearTree()
{
    TreeView_DeleteAllItems(m_tree);
    m_root = nullptr;
}

void DockPanel::ResetRoot(const std::wstring& label, bool connected)
{
    // The root node is the anchor the user always sees, so it is recreated
    // rather than left absent when there is no profile or no connection.
    TreeView_DeleteAllItems(m_tree);

    auto* data = new NodeData();
    data->kind = NodeKind::Root;
    data->loaded = !connected; // nothing to fetch while disconnected

    TVINSERTSTRUCTW ins{};
    ins.hParent = TVI_ROOT;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    std::wstring text = label;
    ins.item.pszText = text.data();
    ins.item.lParam = reinterpret_cast<LPARAM>(data);
    ins.item.cChildren = connected ? 1 : 0;
    ins.item.iImage = static_cast<int>(TreeIcon::Root);
    ins.item.iSelectedImage = static_cast<int>(TreeIcon::Root);
    m_root = TreeView_InsertItem(m_tree, &ins);
    TreeView_SelectItem(m_tree, m_root);
}

void DockPanel::SetPlaceholder(HTREEITEM parent, const wchar_t* text)
{
    ClearPlaceholders(parent);

    auto* data = new NodeData();
    data->kind = NodeKind::Placeholder;

    TVINSERTSTRUCTW ins{};
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    std::wstring label = text;
    ins.item.pszText = label.data();
    ins.item.lParam = reinterpret_cast<LPARAM>(data);
    ins.item.cChildren = 0;
    TreeView_InsertItem(m_tree, &ins);

    TVITEMW tvi{};
    tvi.mask = TVIF_HANDLE | TVIF_CHILDREN;
    tvi.hItem = parent;
    tvi.cChildren = 1;
    TreeView_SetItem(m_tree, &tvi);
    TreeView_Expand(m_tree, parent, TVE_EXPAND);
}

void DockPanel::ClearPlaceholders(HTREEITEM parent)
{
    HTREEITEM child = TreeView_GetChild(m_tree, parent);
    while (child)
    {
        HTREEITEM next = TreeView_GetNextSibling(m_tree, child);
        NodeData* data = DataOf(child);
        if (data && data->kind == NodeKind::Placeholder)
            TreeView_DeleteItem(m_tree, child);
        child = next;
    }
}

HTREEITEM DockPanel::AddBucket(const std::string& bucket)
{
    auto* data = new NodeData();
    data->kind = NodeKind::Bucket;
    data->bucket = bucket;

    TVINSERTSTRUCTW ins{};
    // Buckets hang off the always-present root node.
    ins.hParent = m_root ? m_root : TVI_ROOT;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    std::wstring label = Utf8ToWide(bucket);
    ins.item.pszText = label.data();
    ins.item.lParam = reinterpret_cast<LPARAM>(data);
    ins.item.cChildren = 1; // show expander before loading
    ins.item.iImage = static_cast<int>(TreeIcon::Bucket);
    ins.item.iSelectedImage = static_cast<int>(TreeIcon::Bucket);
    return TreeView_InsertItem(m_tree, &ins);
}

void DockPanel::SetNodeLoading(HTREEITEM item, bool loading)
{
    if (NodeData* data = DataOf(item))
        data->loading = loading;
}

void DockPanel::AppendListing(HTREEITEM item, const std::string& parentPrefix,
                              const ListObjectsResult& listing)
{
    NodeData* parent = DataOf(item);
    if (!parent)
        return;

    for (const std::string& prefix : listing.commonPrefixes)
    {
        std::string name = prefix.substr(parentPrefix.size());
        if (!name.empty() && name.back() == '/')
            name.pop_back();
        if (name.empty())
            continue;

        auto* data = new NodeData();
        data->kind = NodeKind::Prefix;
        data->bucket = parent->bucket;
        data->key = prefix;

        TVINSERTSTRUCTW ins{};
        ins.hParent = item;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        std::wstring label = Utf8ToWide(name);
        ins.item.pszText = label.data();
        ins.item.lParam = reinterpret_cast<LPARAM>(data);
        ins.item.cChildren = 1;
        ins.item.iImage = static_cast<int>(TreeIcon::Folder);
        ins.item.iSelectedImage = static_cast<int>(TreeIcon::Folder);
        TreeView_InsertItem(m_tree, &ins);
    }

    for (const ObjectInfo& obj : listing.objects)
    {
        // The zero-byte folder-marker for the prefix itself is not a child.
        if (obj.key == parentPrefix)
            continue;
        std::string name = obj.key.substr(parentPrefix.size());
        if (name.empty())
            continue;

        auto* data = new NodeData();
        data->kind = NodeKind::Object;
        data->bucket = parent->bucket;
        data->key = obj.key;
        data->size = obj.size;
        data->etag = obj.etag;
        data->lastModified = obj.lastModified;

        TVINSERTSTRUCTW ins{};
        ins.hParent = item;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        std::wstring label = Utf8ToWide(name);
        ins.item.pszText = label.data();
        ins.item.lParam = reinterpret_cast<LPARAM>(data);
        ins.item.cChildren = 0;
        ins.item.iImage = static_cast<int>(TreeIcon::File);
        ins.item.iSelectedImage = static_cast<int>(TreeIcon::File);
        TreeView_InsertItem(m_tree, &ins);
    }
}

void DockPanel::FinishNode(HTREEITEM item)
{
    NodeData* data = DataOf(item);
    if (!data)
        return;
    data->loaded = true;
    data->loading = false;

    ClearPlaceholders(item);
    if (!TreeView_GetChild(m_tree, item))
    {
        // Keep the node expandable so an empty prefix still reads as a
        // container rather than silently looking like a leaf.
        SetPlaceholder(item, T(StrId::TreeEmpty));
        return;
    }

    TVITEMW tvi{};
    tvi.mask = TVIF_HANDLE | TVIF_CHILDREN;
    tvi.hItem = item;
    tvi.cChildren = 1;
    TreeView_SetItem(m_tree, &tvi);
    TreeView_Expand(m_tree, item, TVE_EXPAND);
}

void DockPanel::RemoveChildren(HTREEITEM item)
{
    HTREEITEM child = TreeView_GetChild(m_tree, item);
    while (child)
    {
        HTREEITEM next = TreeView_GetNextSibling(m_tree, child);
        TreeView_DeleteItem(m_tree, child);
        child = next;
    }
    if (NodeData* data = DataOf(item))
    {
        data->loaded = false;
        data->loading = false;
    }
}

NodeData* DockPanel::DataOf(HTREEITEM item) const
{
    if (!item)
        return nullptr;
    TVITEMW tvi{};
    tvi.mask = TVIF_HANDLE | TVIF_PARAM;
    tvi.hItem = item;
    if (!TreeView_GetItem(m_tree, &tvi))
        return nullptr;
    return reinterpret_cast<NodeData*>(tvi.lParam);
}

HTREEITEM DockPanel::SelectedItem() const
{
    return TreeView_GetSelection(m_tree);
}

HTREEITEM DockPanel::ParentOf(HTREEITEM item) const
{
    return TreeView_GetParent(m_tree, item);
}

HTREEITEM DockPanel::FindBucketNode(const std::string& bucket) const
{
    if (!m_root)
        return nullptr;
    for (HTREEITEM it = TreeView_GetChild(m_tree, m_root); it;
         it = TreeView_GetNextSibling(m_tree, it))
    {
        NodeData* data = DataOf(it);
        if (data && data->kind == NodeKind::Bucket && data->bucket == bucket)
            return it;
    }
    return nullptr;
}

HTREEITEM DockPanel::FindChildPrefixNode(HTREEITEM bucketItem, const std::string& prefix) const
{
    // Walks down from the bucket node following prefix segments.
    HTREEITEM current = bucketItem;
    if (!current)
        return nullptr;
    if (prefix.empty())
        return current;

    for (HTREEITEM child = TreeView_GetChild(m_tree, current); child;
         child = TreeView_GetNextSibling(m_tree, child))
    {
        NodeData* data = DataOf(child);
        if (!data || data->kind != NodeKind::Prefix)
            continue;
        if (data->key == prefix)
            return child;
        if (StartsWith(prefix, data->key))
        {
            HTREEITEM deeper = FindChildPrefixNode(child, prefix);
            if (deeper)
                return deeper;
        }
    }
    return nullptr;
}

// -------------------------------------------------------------- transfer list

int DockPanel::TransferRowById(unsigned long long id) const
{
    int count = ListView_GetItemCount(m_transfers);
    for (int i = 0; i < count; ++i)
    {
        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = i;
        if (ListView_GetItem(m_transfers, &item) &&
            static_cast<unsigned long long>(item.lParam) == id)
            return i;
    }
    return -1;
}

void DockPanel::UpsertTransfer(const TransferEvent& ev)
{
    int row = TransferRowById(ev.id);
    if (row < 0)
    {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = ListView_GetItemCount(m_transfers);
        std::wstring op = LocalizedOp(ev.op);
        item.pszText = op.data();
        item.lParam = static_cast<LPARAM>(ev.id);
        row = ListView_InsertItem(m_transfers, &item);
        if (row < 0)
            return;
        ListView_SetItemText(m_transfers, row, 1, const_cast<wchar_t*>(ev.label.c_str()));
    }

    std::wstring progress;
    if (ev.type == TransferEvent::Type::Progress)
    {
        if (ev.total > 0)
        {
            wchar_t buf[32];
            ::_snwprintf_s(buf, _TRUNCATE, L"%u%%",
                           static_cast<unsigned>(ev.transferred * 100 / ev.total));
            progress = buf;
        }
        else
        {
            progress = FormatBytes(ev.transferred);
        }
    }
    else if (ev.state == TransferState::Completed)
    {
        progress = L"100%";
    }
    if (!progress.empty())
        ListView_SetItemText(m_transfers, row, 2, progress.data());

    std::wstring status = LocalizedState(ev.state);
    if (ev.state == TransferState::Failed && !ev.error.s3Code.empty())
        status += L" (" + Utf8ToWide(ev.error.s3Code) + L")";
    ListView_SetItemText(m_transfers, row, 3, status.data());
    ListView_EnsureVisible(m_transfers, row, FALSE);
}

void DockPanel::ClearFinishedTransfers()
{
    // Walk backwards so removals do not shift later indices.
    for (int i = ListView_GetItemCount(m_transfers) - 1; i >= 0; --i)
    {
        wchar_t status[64]{};
        ListView_GetItemText(m_transfers, i, 3, status, 63);
        std::wstring s = status;
        if (s == T(StrId::StateCompleted) || s == T(StrId::StateCancelled) ||
            s.find(T(StrId::StateFailed)) == 0)
            ListView_DeleteItem(m_transfers, i);
    }
}

// ------------------------------------------------------------------------ log

void DockPanel::AppendLog(LogLevel level, const std::wstring& text)
{
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t prefix[32];
    ::_snwprintf_s(prefix, _TRUNCATE, L"%02u:%02u:%02u ", st.wHour, st.wMinute, st.wSecond);

    std::wstring line = prefix;
    if (level == LogLevel::Error)
        line += L"[!] ";
    line += text;
    line += L"\r\n";

    int len = ::GetWindowTextLengthW(m_log);
    // Trim the top when the log grows too large.
    if (len > 64 * 1024)
    {
        ::SendMessageW(m_log, EM_SETSEL, 0, len / 2);
        ::SendMessageW(m_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
        len = ::GetWindowTextLengthW(m_log);
    }
    ::SendMessageW(m_log, EM_SETSEL, len, len);
    ::SendMessageW(m_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

// -------------------------------------------------------------------- profile

void DockPanel::ReloadProfiles()
{
    if (!m_profileCombo)
        return;
    auto& mgr = NppS3Plugin::Instance().Profiles();
    ::SendMessageW(m_profileCombo, CB_RESETCONTENT, 0, 0);
    m_profileIds.clear();
    int select = -1;
    for (const Profile& p : mgr.Profiles())
    {
        ::SendMessageW(m_profileCombo, CB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(Utf8ToWide(p.name).c_str()));
        if (p.id == mgr.Settings().activeProfileId)
            select = static_cast<int>(m_profileIds.size());
        m_profileIds.push_back(p.id);
    }
    if (select < 0 && !m_profileIds.empty())
        select = 0;
    if (select >= 0)
        ::SendMessageW(m_profileCombo, CB_SETCURSEL, select, 0);
}

std::string DockPanel::SelectedProfileId() const
{
    int sel = static_cast<int>(::SendMessageW(m_profileCombo, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(m_profileIds.size()))
        return {};
    return m_profileIds[static_cast<size_t>(sel)];
}

void DockPanel::SelectProfile(const std::string& id)
{
    for (size_t i = 0; i < m_profileIds.size(); ++i)
    {
        if (m_profileIds[i] == id)
        {
            ::SendMessageW(m_profileCombo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return;
        }
    }
}

void DockPanel::SetConnectedUi(bool connected)
{
    m_connected = connected;
}

void DockPanel::Relocalize()
{
    if (!m_hwnd)
        return;
    UpdateTooltips();

    const StrId colIds[] = {StrId::ColOperation, StrId::ColObject,
                            StrId::ColProgress, StrId::ColStatus};
    for (int i = 0; i < 4; ++i)
    {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT;
        col.pszText = const_cast<wchar_t*>(T(colIds[i]));
        ListView_SetColumn(m_transfers, i, &col);
    }
}

} // namespace npps3
