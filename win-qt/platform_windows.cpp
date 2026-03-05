/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * platform_windows.cpp — Windows platform integration stub
 *
 * Windows API: Shell_NotifyIconW (system tray / badge),
 *              WinRT ToastNotificationManager (toast notifications)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "platform_windows.h"

namespace mc1 {
namespace platform {

void macos_init()
{
    // TODO: Implement with Windows API
    // Windows equivalent: CoInitializeEx, DPI awareness, etc.
}

void set_dock_badge(const char * /*text*/)
{
    // TODO: Implement with Windows API
    // Windows equivalent: ITaskbarList3::SetOverlayIcon or
    // Shell_NotifyIconW with custom icon overlay
}

void clear_dock_badge()
{
    // TODO: Implement with Windows API
    // Windows equivalent: ITaskbarList3::SetOverlayIcon(nullptr)
}

void request_notification_permission()
{
    // TODO: Implement with Windows API
    // No-op on Windows — toast notifications don't require explicit permission
}

void send_notification(const char * /*title*/, const char * /*body*/)
{
    // TODO: Implement with Windows API
    // Windows equivalent: WinRT ToastNotificationManager or
    // Shell_NotifyIconW with NIF_INFO balloon tooltip
}

} // namespace platform
} // namespace mc1
