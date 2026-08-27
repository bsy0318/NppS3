// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../cache/CacheManager.h"
#include "../config/ProfileManager.h"
#include "../documents/RemoteDocumentManager.h"
#include "../transfer/TransferManager.h"
#include "../ui/DockPanel.h"

#include "../npp/PluginInterface.h"

#include <map>
#include <memory>
#include <string>

namespace npps3 {

// Central controller: owns managers, routes Notepad++ notifications, panel
// actions and transfer completions. Lives on the UI thread except for
// OnTransferEvent, which only posts a message.
class NppS3Plugin : public ITransferObserver
{
public:
    static NppS3Plugin& Instance();

    // Lifecycle
    void OnDllAttach(HINSTANCE hModule);
    void SetNppData(const NppData& data) { m_npp = data; }
    void OnReady();
    void OnToolbarModification();
    void OnShutdown();
    void OnNotification(SCNotification* n);

    HWND NppHandle() const { return m_npp._nppHandle; }
    HINSTANCE ModuleHandle() const { return m_hModule; }
    bool Started() const { return m_started; }

    // Menu commands (also invoked by Notepad++ to restore the panel).
    void CmdShowPanel();
    void CmdUploadCurrent();
    void CmdProfiles();
    void CmdSettings();
    void CmdAbout();

    // Settings dialog helpers.
    const std::wstring& CacheRoot() const { return m_cache.Root(); }
    // Removes cache files not referenced by an open remote document.
    int ClearCacheNow();

    // Called by PluginMain once cmd ids are assigned.
    void SetShowPanelCmdId(int cmdId) { m_showPanelCmdId = cmdId; }

    ProfileManager& Profiles() { return m_profiles; }
    const Profile* ActiveProfile() const;
    bool Connected() const { return m_connected; }

    // Panel-driven actions (UI thread).
    void ConnectActiveProfile();
    void Disconnect();
    void RefreshTree();
    // Rebuilds the root node to match the current profile/connection state.
    void UpdateTreeRoot();
    void RequestChildren(HTREEITEM item);
    void RefreshNode(HTREEITEM item);
    void OpenObject(HTREEITEM item);
    void DownloadObjectAs(HTREEITEM item);
    void DeleteObjectAsk(HTREEITEM item);
    void DeletePrefixAsk(HTREEITEM item);
    void RenameObjectAsk(HTREEITEM item);
    void NewFileAsk(HTREEITEM item);
    void NewFolderAsk(HTREEITEM item);
    void UploadFileHereAsk(HTREEITEM item);
    void CopyKeyToClipboard(HTREEITEM item);
    void CopyUriToClipboard(HTREEITEM item);
    void ShowProperties(HTREEITEM item);
    void CancelTransfer(unsigned long long id);
    void OnProfileSelectionChanged(const std::string& profileId);
    void OnPanelClosedByUser();

    // Runs a synchronous connection test with explicit settings (used by the
    // profile dialog "Test Connection" on a temporary worker).
    VoidResult TestProfileConnection(const Profile& p, const std::string& secretOverride);

    // Transfer observer (worker thread): marshal to UI.
    void OnTransferEvent(const TransferEvent& ev) override;
    // UI-thread handlers invoked from the panel window proc.
    void HandleTransferEventUi(TransferEvent* ev); // takes ownership
    void ApplyLanguageSetting();

private:
    enum class ActionKind
    {
        None,
        ConnectTest,
        ListBucketsRoot,
        ListNode,
        DownloadOpen,
        DownloadAs,
        HeadForSave,
        UploadSave,
        UploadManual,
        CreateObject,
        RenameCopy,
        RenameDelete,
        DeleteOne,
        DeletePrefixPage,
        DeletePrefixItem,
        PropertiesHead,
        SaveConflictDownload,
    };

    struct PendingAction
    {
        ActionKind kind = ActionKind::None;
        HTREEITEM item = nullptr;      // tree node the result belongs to
        std::string bucket;
        std::string key;               // primary key/prefix
        std::string dstKey;            // rename target
        std::wstring localPath;
        int page = 0;
        std::shared_ptr<int> counter;  // shared across bulk deletes
    };

    struct SaveState
    {
        bool inFlight = false;
        bool again = false;
    };

    NppS3Plugin() = default;

    S3Config BuildConfig(const Profile& p, std::string* error) const;
    unsigned long long Enqueue(TransferRequest req, PendingAction action);
    void StartListing(HTREEITEM item, const std::string& bucket,
                      const std::string& prefix, const std::string& token, int page);
    void HandleFileSaved(uintptr_t bufferId);
    void StartSaveUpload(const std::wstring& localPath);
    void FinishSave(const std::wstring& localPath, bool restartIfDirty);
    std::wstring PathFromBufferId(uintptr_t bufferId) const;
    std::wstring CurrentDocPath() const;
    std::wstring ConfigDir() const;
    void LocalizeMenu();
    void UpdateShowPanelCheck(bool shown);
    void ShowError(const StorageError& e, const std::wstring& contextMsg);
    void RefreshParentOfKey(const std::string& bucket, const std::string& key);

    NppData m_npp{};
    HINSTANCE m_hModule = nullptr;
    bool m_started = false;
    bool m_connected = false;
    bool m_panelRegistered = false;
    bool m_panelVisible = false;
    int m_showPanelCmdId = 0;

    ProfileManager m_profiles;
    RemoteDocumentManager m_documents;
    CacheManager m_cache;
    std::unique_ptr<TransferManager> m_transfers;
    DockPanel m_panel;

    S3Config m_activeConfig; // includes secret while connected (memory only)
    std::map<unsigned long long, PendingAction> m_pending;
    std::map<std::wstring, SaveState> m_saveStates; // key: normalized local path
};

} // namespace npps3
