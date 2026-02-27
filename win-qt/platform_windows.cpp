/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * platform_windows.cpp — Windows platform integration
 *
 * ITaskbarList3 for taskbar badge overlay,
 * Shell_NotifyIconW for balloon notifications.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <combaseapi.h>

#include "platform_windows.h"
#include <cstring>

namespace mc1 {
namespace platform {

/* ── Taskbar badge via ITaskbarList3 ─────────────────────────────────── */

static ITaskbarList3 *g_taskbar = nullptr;
static bool           g_taskbar_init = false;

static HWND find_main_hwnd()
{
    /* Find the first visible top-level window on the current thread.
     * Qt creates its main window on the GUI thread, so this finds it. */
    struct Ctx { HWND result; };
    Ctx ctx{nullptr};

    EnumThreadWindows(GetCurrentThreadId(),
        [](HWND hwnd, LPARAM lp) -> BOOL {
            if (IsWindowVisible(hwnd)) {
                reinterpret_cast<Ctx*>(lp)->result = hwnd;
                return FALSE; /* stop enumerating */
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));

    return ctx.result;
}

static void ensure_taskbar()
{
    if (g_taskbar_init) return;
    g_taskbar_init = true;

    HRESULT hr = CoCreateInstance(
        CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITaskbarList3, reinterpret_cast<void**>(&g_taskbar));

    if (SUCCEEDED(hr) && g_taskbar) {
        g_taskbar->HrInit();
    } else {
        g_taskbar = nullptr;
    }
}

/* Create a small 16x16 HICON with text rendered on a colored circle */
static HICON create_badge_icon(const char *text)
{
    HDC screen_dc = GetDC(nullptr);
    HDC mem_dc = CreateCompatibleDC(screen_dc);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 16;
    bmi.bmiHeader.biHeight = -16; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP color_bmp = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP old_bmp = static_cast<HBITMAP>(SelectObject(mem_dc, color_bmp));

    /* Red circle background */
    HBRUSH red = CreateSolidBrush(RGB(220, 38, 38));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(220, 38, 38));
    SelectObject(mem_dc, red);
    SelectObject(mem_dc, pen);
    Ellipse(mem_dc, 0, 0, 16, 16);

    /* White text */
    SetTextColor(mem_dc, RGB(255, 255, 255));
    SetBkMode(mem_dc, TRANSPARENT);

    HFONT font = CreateFontA(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HFONT old_font = static_cast<HFONT>(SelectObject(mem_dc, font));

    RECT rc{0, 1, 16, 16};
    DrawTextA(mem_dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(mem_dc, old_font);
    DeleteObject(font);
    DeleteObject(red);
    DeleteObject(pen);
    SelectObject(mem_dc, old_bmp);

    /* Create mask bitmap (all opaque) */
    HBITMAP mask_bmp = CreateBitmap(16, 16, 1, 1, nullptr);
    HDC mask_dc = CreateCompatibleDC(screen_dc);
    HBITMAP old_mask = static_cast<HBITMAP>(SelectObject(mask_dc, mask_bmp));

    RECT full{0, 0, 16, 16};
    FillRect(mask_dc, &full, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    SelectObject(mask_dc, old_mask);
    DeleteDC(mask_dc);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask_bmp;
    ii.hbmColor = color_bmp;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(color_bmp);
    DeleteObject(mask_bmp);
    DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);

    return icon;
}

void windows_init()
{
    /* Ensure COM is initialized for STA (Qt usually does this) */
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    /* DPI awareness — Qt 6 handles this, but belt-and-suspenders */
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

void set_dock_badge(const char *text)
{
    ensure_taskbar();
    if (!g_taskbar) return;

    HWND hwnd = find_main_hwnd();
    if (!hwnd) return;

    if (text && text[0]) {
        HICON badge = create_badge_icon(text);
        if (badge) {
            wchar_t desc[64]{};
            MultiByteToWideChar(CP_UTF8, 0, text, -1, desc, 63);
            g_taskbar->SetOverlayIcon(hwnd, badge, desc);
            DestroyIcon(badge);
        }
    } else {
        g_taskbar->SetOverlayIcon(hwnd, nullptr, L"");
    }
}

void clear_dock_badge()
{
    ensure_taskbar();
    if (!g_taskbar) return;

    HWND hwnd = find_main_hwnd();
    if (hwnd)
        g_taskbar->SetOverlayIcon(hwnd, nullptr, L"");
}

void request_notification_permission()
{
    /* No-op on Windows — notifications don't require explicit permission */
}

void send_notification(const char *title, const char *body)
{
    /* Use Shell_NotifyIconW balloon notification.
     * This is a fallback — the app's QSystemTrayIcon::showMessage() is preferred. */
    HWND hwnd = find_main_hwnd();

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd ? hwnd : GetDesktopWindow();
    nid.uID = 1;
    nid.uFlags = NIF_INFO | NIF_ICON;
    nid.dwInfoFlags = NIIF_INFO;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    if (title)
        MultiByteToWideChar(CP_UTF8, 0, title, -1, nid.szInfoTitle, 63);
    if (body)
        MultiByteToWideChar(CP_UTF8, 0, body, -1, nid.szInfo, 255);

    /* Try modify first (if tray icon exists), then add */
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid))
        Shell_NotifyIconW(NIM_ADD, &nid);
}

} // namespace platform
} // namespace mc1
