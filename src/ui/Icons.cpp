// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Icons.h"

#include "resource.h"

#include <objidl.h>
#include <gdiplus.h>

#include <initializer_list>

#pragma comment(lib, "gdiplus.lib")

namespace npps3 {
namespace {

HINSTANCE g_module = nullptr;
ULONG_PTR g_gdiplusToken = 0;
HIMAGELIST g_treeImages = nullptr;
HICON g_connect = nullptr;
HICON g_refresh = nullptr;
HICON g_upload = nullptr;
HICON g_settings = nullptr;
Icons::ToolbarIcons g_toolbar;

// Decodes an RCDATA PNG resource into a GDI+ bitmap.
Gdiplus::Bitmap* LoadPngResource(int resourceId)
{
    HRSRC res = ::FindResourceW(g_module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!res)
        return nullptr;
    DWORD size = ::SizeofResource(g_module, res);
    HGLOBAL data = ::LoadResource(g_module, res);
    if (!data || size == 0)
        return nullptr;
    void* bytes = ::LockResource(data);
    if (!bytes)
        return nullptr;

    HGLOBAL copy = ::GlobalAlloc(GMEM_MOVEABLE, size);
    if (!copy)
        return nullptr;
    void* dst = ::GlobalLock(copy);
    ::memcpy(dst, bytes, size);
    ::GlobalUnlock(copy);

    IStream* stream = nullptr;
    if (FAILED(::CreateStreamOnHGlobal(copy, TRUE, &stream)))
    {
        ::GlobalFree(copy);
        return nullptr;
    }
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(stream);
    stream->Release(); // owns and frees the HGLOBAL
    if (bmp && bmp->GetLastStatus() != Gdiplus::Ok)
    {
        delete bmp;
        return nullptr;
    }
    return bmp;
}

HICON IconFromResource(int resourceId)
{
    Gdiplus::Bitmap* bmp = LoadPngResource(resourceId);
    if (!bmp)
        return nullptr;
    HICON icon = nullptr;
    bmp->GetHICON(&icon);
    delete bmp;
    return icon;
}

// Draws the Silk folder with "SR" at the bottom-right onto a 16x16 ARGB
// bitmap. darkMode selects a light text color for dark toolbars.
Gdiplus::Bitmap* ComposePanelToggle(bool darkMode)
{
    Gdiplus::Bitmap* folder = LoadPngResource(IDR_PNG_FOLDER);
    auto* canvas = new Gdiplus::Bitmap(16, 16, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(canvas);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        if (folder)
            g.DrawImage(folder, 0, 0, 16, 16);

        // "SR" badge across the lower half. Geometry was tuned by rendering
        // candidates at 16x16: a smaller plate clips the R at this size.
        Gdiplus::RectF badge(2.0f, 8.0f, 14.0f, 8.0f);
        Gdiplus::SolidBrush plate(darkMode ? Gdiplus::Color(255, 225, 228, 240)
                                           : Gdiplus::Color(255, 30, 55, 130));
        g.FillRectangle(&plate, badge);

        g.SetTextRenderingHint(Gdiplus::TextRenderingHintSingleBitPerPixelGridFit);
        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::Font font(&family, 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush text(darkMode ? Gdiplus::Color(255, 25, 40, 100)
                                          : Gdiplus::Color(255, 255, 255, 255));
        g.DrawString(L"SR", 2, &font, badge, &fmt, &text);
    }
    delete folder;
    return canvas;
}

void AddToImageList(HIMAGELIST list, int resourceId)
{
    Gdiplus::Bitmap* bmp = LoadPngResource(resourceId);
    if (!bmp)
        return;
    HICON icon = nullptr;
    bmp->GetHICON(&icon);
    if (icon)
    {
        ::ImageList_AddIcon(list, icon);
        ::DestroyIcon(icon);
    }
    delete bmp;
}

} // namespace

void Icons::Init(HINSTANCE module)
{
    if (g_gdiplusToken)
        return;
    g_module = module;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) != Gdiplus::Ok)
    {
        g_gdiplusToken = 0;
        return;
    }

    g_treeImages = ::ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 4, 2);
    AddToImageList(g_treeImages, IDR_PNG_DATABASE);   // TreeIcon::Bucket
    AddToImageList(g_treeImages, IDR_PNG_FOLDER);     // TreeIcon::Folder
    AddToImageList(g_treeImages, IDR_PNG_PAGE);       // TreeIcon::File
    AddToImageList(g_treeImages, IDR_PNG_CONNECT);    // TreeIcon::Root

    g_connect = IconFromResource(IDR_PNG_CONNECT);
    g_refresh = IconFromResource(IDR_PNG_REFRESH);
    g_upload = IconFromResource(IDR_PNG_UPLOAD);
    g_settings = IconFromResource(IDR_PNG_COG);

    // Main-toolbar icons: light + dark HICONs plus a classic bitmap.
    if (Gdiplus::Bitmap* light = ComposePanelToggle(false))
    {
        light->GetHICON(&g_toolbar.icon);

        // Classic toolbar bitmap: composite over the button-face color.
        auto* flat = new Gdiplus::Bitmap(16, 16, PixelFormat24bppRGB);
        {
            Gdiplus::Graphics g(flat);
            COLORREF face = ::GetSysColor(COLOR_BTNFACE);
            g.Clear(Gdiplus::Color(GetRValue(face), GetGValue(face), GetBValue(face)));
            g.DrawImage(light, 0, 0, 16, 16);
        }
        flat->GetHBITMAP(Gdiplus::Color(255, 255, 255), &g_toolbar.bitmap);
        delete flat;
        delete light;
    }
    if (Gdiplus::Bitmap* dark = ComposePanelToggle(true))
    {
        dark->GetHICON(&g_toolbar.iconDark);
        delete dark;
    }
}

void Icons::Shutdown()
{
    if (g_treeImages)
    {
        ::ImageList_Destroy(g_treeImages);
        g_treeImages = nullptr;
    }
    for (HICON* icon : {&g_connect, &g_refresh, &g_upload, &g_settings,
                        &g_toolbar.icon, &g_toolbar.iconDark})
    {
        if (*icon)
        {
            ::DestroyIcon(*icon);
            *icon = nullptr;
        }
    }
    if (g_toolbar.bitmap)
    {
        ::DeleteObject(g_toolbar.bitmap);
        g_toolbar.bitmap = nullptr;
    }
    if (g_gdiplusToken)
    {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

HIMAGELIST Icons::TreeImageList() { return g_treeImages; }
HICON Icons::Connect() { return g_connect; }
HICON Icons::Refresh() { return g_refresh; }
HICON Icons::Upload() { return g_upload; }
HICON Icons::Settings() { return g_settings; }
const Icons::ToolbarIcons& Icons::PanelToggle() { return g_toolbar; }

} // namespace npps3
