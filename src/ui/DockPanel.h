// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../storage/S3Types.h"
#include "../transfer/TransferManager.h"
#include "../util/Log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

namespace npps3 {

// Marshalled from worker threads to the panel window.
constexpr UINT WM_NPPS3_TRANSFER = WM_APP + 41; // lParam: TransferEvent* (owned)
constexpr UINT WM_NPPS3_LOG = WM_APP + 42;      // lParam: LogLine* (owned)

struct LogLine
{
    LogLevel level;
    std::wstring text;
};

enum class NodeKind { Bucket, Prefix, Object };

struct NodeData
{
    NodeKind kind = NodeKind::Object;
    std::string bucket;
    std::string key;      // full key for objects, full prefix (with trailing '/') for prefixes
    bool loaded = false;  // children fetched (bucket/prefix)
    bool loading = false;
    uint64_t size = 0;
    std::string etag;
    std::string lastModified;
};

// The dockable S3 browser panel: profile selector + action buttons, object
// tree, transfer list, log. Pure UI — every remote action is delegated to
// NppS3Plugin, and all state mutations happen on the UI thread.
class DockPanel
{
public:
    static void RegisterWindowClass(HINSTANCE hInstance);

    bool Create(HWND hNppParent, HINSTANCE hInstance);
    void Destroy();
    HWND Handle() const { return m_hwnd; }
    bool IsCreated() const { return m_hwnd != nullptr; }

    // Tree management (UI thread only).
    void ClearTree();
    HTREEITEM AddBucket(const std::string& bucket);
    void SetNodeLoading(HTREEITEM item, bool loading);
    // Appends one page of listing results under `item`; strips the parent
    // prefix from displayed names.
    void AppendListing(HTREEITEM item, const std::string& parentPrefix,
                       const ListObjectsResult& listing);
    void FinishNode(HTREEITEM item);
    void RemoveChildren(HTREEITEM item);
    NodeData* DataOf(HTREEITEM item) const;
    HTREEITEM SelectedItem() const;
    HTREEITEM ParentOf(HTREEITEM item) const;
    HTREEITEM FindChildPrefixNode(HTREEITEM bucketItem, const std::string& prefix) const;
    HTREEITEM FindBucketNode(const std::string& bucket) const;

    // Transfer list.
    void UpsertTransfer(const TransferEvent& ev);
    void ClearFinishedTransfers();

    void AppendLog(LogLevel level, const std::wstring& text);

    // Profile combo.
    void ReloadProfiles();
    std::string SelectedProfileId() const;
    void SelectProfile(const std::string& id);
    void SetConnectedUi(bool connected);

    void Relocalize();

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void OnCreate(HWND hwnd);
    void OnSize(int width, int height);
    void OnCommand(WPARAM wParam);
    LRESULT OnNotify(NMHDR* hdr);
    void OnTreeContextMenu(int x, int y);
    void OnTransfersContextMenu(int x, int y);
    void OnItemActivated(HTREEITEM item);
    int TransferRowById(unsigned long long id) const;

    HWND m_hwnd = nullptr;
    HWND m_profileCombo = nullptr;
    HWND m_btnConnect = nullptr;
    HWND m_btnRefresh = nullptr;
    HWND m_btnUpload = nullptr;
    HWND m_btnProfiles = nullptr;
    HWND m_tree = nullptr;
    HWND m_transfers = nullptr;
    HWND m_log = nullptr;
    HFONT m_font = nullptr;
    bool m_connected = false;
    std::vector<std::string> m_profileIds; // combo index -> profile id
};

} // namespace npps3
