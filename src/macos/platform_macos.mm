/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * platform_macos.mm — macOS platform integration (Obj-C++)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "platform_macos.h"

#import <Cocoa/Cocoa.h>
#import <UserNotifications/UserNotifications.h>

/* UNUserNotificationCenter requires a valid app bundle with a bundle
   identifier.  When running the bare binary (not inside .app), calling
   currentNotificationCenter crashes with NSInternalInconsistencyException.
   Guard all UN calls behind a bundle-identifier check. */
static bool has_bundle_id()
{
    NSBundle *b = [NSBundle mainBundle];
    return b && b.bundleIdentifier && b.bundleIdentifier.length > 0;
}

namespace mc1 {
namespace platform {

void macos_init()
{
    /* Ensure the app activates properly when launched from terminal */
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
}

void set_dock_badge(const char *text)
{
    if (text && text[0]) {
        [[NSApp dockTile] setBadgeLabel:[NSString stringWithUTF8String:text]];
    } else {
        clear_dock_badge();
    }
}

void clear_dock_badge()
{
    [[NSApp dockTile] setBadgeLabel:nil];
}

void request_notification_permission()
{
    if (!has_bundle_id()) return;

    UNUserNotificationCenter *center =
        [UNUserNotificationCenter currentNotificationCenter];
    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert |
                                             UNAuthorizationOptionSound)
                          completionHandler:^(BOOL /* granted */, NSError *error) {
        if (error) {
            NSLog(@"Mcaster1: notification permission error: %@", error);
        }
    }];
}

void send_notification(const char *title, const char *body)
{
    if (!has_bundle_id()) return;

    UNUserNotificationCenter *center =
        [UNUserNotificationCenter currentNotificationCenter];

    UNMutableNotificationContent *content =
        [[UNMutableNotificationContent alloc] init];
    content.title = [NSString stringWithUTF8String:title];
    content.body  = [NSString stringWithUTF8String:body];
    content.sound = [UNNotificationSound defaultSound];

    /* Unique identifier so each notification replaces the previous one */
    UNNotificationRequest *request =
        [UNNotificationRequest requestWithIdentifier:@"mc1_encoder_state"
                                             content:content
                                             trigger:nil];

    [center addNotificationRequest:request
             withCompletionHandler:^(NSError *error) {
        if (error) {
            NSLog(@"Mcaster1: notification delivery error: %@", error);
        }
    }];
}

} // namespace platform
} // namespace mc1
