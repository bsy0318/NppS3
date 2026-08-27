// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace npps3 {

// UI languages. Auto resolves from the Notepad++ native-language file at
// NPPN_READY; English is the fallback.
enum class Lang
{
    Auto = 0,
    EN,
    KO,
    JA,
    ZH,
    RU,
};

enum class StrId
{
    // Plugins menu
    MenuShowPanel,
    MenuUploadCurrent,
    MenuProfiles,
    MenuSettings,
    MenuAbout,

    // Panel chrome
    PanelTitle,
    BtnConnect,
    BtnRefresh,
    BtnUpload,
    BtnProfiles,
    BtnGear,

    // Transfer list columns / states
    ColOperation,
    ColObject,
    ColProgress,
    ColStatus,
    StatePending,
    StateRunning,
    StateCompleted,
    StateFailed,
    StateCancelled,

    // Operations (display)
    OpConnect,
    OpList,
    OpDownload,
    OpUpload,
    OpDelete,
    OpCopy,
    OpCreate,
    OpProperties,

    // Context menu — object
    CtxOpen,
    CtxDownloadAs,
    CtxRename,
    CtxCopyKey,
    CtxCopyUri,
    CtxDelete,
    CtxProperties,

    // Context menu — prefix/bucket
    CtxNewFile,
    CtxNewFolder,
    CtxUploadHere,
    CtxRefresh,

    // Context menu — transfers
    CtxCancel,
    CtxClearFinished,

    // Tree placeholders
    TreeNoProfile,
    TreeDisconnected,   // %s = profile name
    TreeConnecting,
    TreeEmpty,
    TreeLoading,
    CtxConnect,
    CtxDisconnect,

    // Messages
    MsgConnected,
    MsgConnectFailed,
    MsgNoProfile,
    MsgNotConnected,
    MsgConfirmDeleteObject,   // %s = key
    MsgConfirmDeletePrefix,   // %s = prefix
    MsgUploadDone,            // %s = key
    MsgDownloadDone,          // %s = key
    MsgDeleted,               // %s = key
    MsgRenamed,               // %s = new key
    MsgErrorTitle,
    MsgConflictTitle,
    MsgConflictBody,          // %s = key
    MsgRemoteGoneBody,        // %s = key
    MsgSelectProfileFirst,
    MsgNoDocument,
    MsgKeyCopied,

    // Conflict choices
    BtnOverwrite,
    BtnDownloadRemote,
    BtnCancel,

    // Profiles dialog
    DlgProfilesTitle,
    LblProfileList,
    LblProfileName,
    LblProvider,
    LblEndpoint,
    LblRegion,
    LblAccessKey,
    LblSecretKey,
    LblSecretHint,
    LblDefaultBucket,
    LblDefaultPrefix,
    ChkPathStyle,
    ChkAutoUpload,
    BtnNew,
    BtnDuplicate,
    BtnDelete,
    BtnSave,
    BtnTest,
    BtnClose,
    MsgProfileSaved,
    MsgTestOk,
    MsgTestFail,
    MsgFieldsRequired,
    MsgSecretRequired,
    MsgConfirmDeleteProfile,  // %s = profile name
    ProfileCopySuffix,        // appended to a duplicated profile's name

    // General settings dialog
    DlgSettingsTitle,
    LblLanguage,
    LblStaleCacheDays,
    LblStaleCacheHint,
    LblCacheLocation,
    BtnOpenCacheFolder,
    BtnClearCacheNow,
    MsgCacheCleared,          // %d = files removed
    MsgConfirmClearCache,
    GrpAppearance,
    GrpCache,

    // Upload dialog
    DlgUploadTitle,
    LblUploadProfile,
    LblBucket,
    LblKey,
    LblContentType,
    BtnOk,

    // Input dialogs
    DlgNewFileTitle,
    DlgNewFolderTitle,
    DlgRenameTitle,
    LblName,

    // Properties
    PropTitle,
    PropSize,
    PropLastModified,
    PropETag,
    PropContentType,
    PropStorageClass,
    PropVersionId,

    // About
    AboutTitle,
    AboutBody,

    COUNT_,
};

void SetLanguage(Lang lang);
Lang CurrentLanguage();

// Maps a Notepad++ native-language file name ("korean.xml", ...) to a Lang.
Lang DetectLanguage(const std::string& nativeLangFileName);

const char* LangToString(Lang lang);
Lang LangFromString(const std::string& s);

// Localized string lookup; never returns null.
const wchar_t* T(StrId id);

} // namespace npps3
