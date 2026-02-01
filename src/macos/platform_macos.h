/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * platform_macos.h — macOS platform integration
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_PLATFORM_MACOS_H
#define MC1_PLATFORM_MACOS_H

namespace mc1 {
namespace platform {

/* Initialize macOS-specific behavior (call from main before QApplication) */
void macos_init();

/* Set dock badge text (e.g. "3" for 3 live slots) */
void set_dock_badge(const char *text);

/* Clear dock badge */
void clear_dock_badge();

/* Request permission for native macOS notifications (call once at startup) */
void request_notification_permission();

/* Send a native macOS notification via UNUserNotificationCenter */
void send_notification(const char *title, const char *body);

} // namespace platform
} // namespace mc1

#endif // MC1_PLATFORM_MACOS_H
