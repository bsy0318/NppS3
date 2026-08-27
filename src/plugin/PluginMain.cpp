// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Exported Notepad++ plugin entry points.

#include "NppS3Plugin.h"

#include <commctrl.h>

namespace {

npps3::NppS3Plugin& Plugin()
{
    return npps3::NppS3Plugin::Instance();
}

void CmdShowPanel() { Plugin().CmdShowPanel(); }
void CmdUploadCurrent() { Plugin().CmdUploadCurrent(); }
void CmdProfiles() { Plugin().CmdProfiles(); }
void CmdSettings() { Plugin().CmdSettings(); }
void CmdAbout() { Plugin().CmdAbout(); }

// Registered at load time in English; relabeled with the localized strings
// at NPPN_READY (menu item text is mutable, FuncItem text is not).
FuncItem g_funcItems[] = {
    {L"Show S3 Panel", CmdShowPanel, 0, false, nullptr},
    {L"Upload Current File...", CmdUploadCurrent, 0, false, nullptr},
    {L"Profiles...", CmdProfiles, 0, false, nullptr},
    {L"Settings...", CmdSettings, 0, false, nullptr},
    {L"About NppS3", CmdAbout, 0, false, nullptr},
};

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        ::DisableThreadLibraryCalls(hModule);
        INITCOMMONCONTROLSEX icc{sizeof(icc),
                                 ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES |
                                     ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
        ::InitCommonControlsEx(&icc);
        Plugin().OnDllAttach(static_cast<HINSTANCE>(hModule));
        break;
    }
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData nppData)
{
    Plugin().SetNppData(nppData);
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
    return L"NppS3";
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count)
{
    *count = static_cast<int>(std::size(g_funcItems));
    return g_funcItems;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notification)
{
    // The cmd id is assigned by Notepad++ after getFuncsArray; capture it
    // before the READY handler needs it for menu localization/checkmarks.
    Plugin().SetShowPanelCmdId(g_funcItems[0]._cmdID);
    Plugin().OnNotification(notification);
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{
    return TRUE;
}
