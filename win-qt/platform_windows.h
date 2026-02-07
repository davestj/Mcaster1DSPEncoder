/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * platform_windows.h — Windows platform integration
 *
 * Windows API: Shell_NotifyIconW (system tray), ToastNotification (WinRT)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_PLATFORM_WINDOWS_H
#define MC1_PLATFORM_WINDOWS_H

namespace mc1 {
namespace platform {

/* Initialize Windows-specific behavior (call from main before QApplication) */
void macos_init();

/* Set taskbar overlay badge text (e.g. "3" for 3 live slots) */
void set_dock_badge(const char *text);

/* Clear taskbar overlay badge */
void clear_dock_badge();

/* Request permission for native Windows notifications (no-op on Windows 10+) */
void request_notification_permission();

/* Send a native Windows toast notification */
void send_notification(const char *title, const char *body);

} // namespace platform
} // namespace mc1

#endif // MC1_PLATFORM_WINDOWS_H
