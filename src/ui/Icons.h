// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

namespace npps3 {

// Tree image-list indices.
enum class TreeIcon : int
{
    Bucket = 0,
    Folder = 1,
    File = 2,
    Root = 3,
};

// Icon assets: famfamfam Silk PNGs (CC-BY 2.5, Mark James) embedded as
// RCDATA, decoded via GDI+ at runtime.
class Icons
{
public:
    static void Init(HINSTANCE module);   // call once (DLL attach / first use)
    static void Shutdown();               // frees GDI+/handles at unload

    // 16x16 image list for the browser tree; owned by Icons.
    static HIMAGELIST TreeImageList();

    // Button icons for the panel toolbar; owned by Icons.
    static HICON Connect();
    static HICON Refresh();
    static HICON Upload();
    static HICON Settings();

    // Main-toolbar toggle icon: Silk folder with "SR" drawn bottom-right.
    // Returned handles are owned by Icons and stay valid until Shutdown.
    struct ToolbarIcons
    {
        HBITMAP bitmap = nullptr;  // classic toolbar bitmap
        HICON icon = nullptr;      // light mode
        HICON iconDark = nullptr;  // dark mode
    };
    static const ToolbarIcons& PanelToggle();
};

} // namespace npps3
