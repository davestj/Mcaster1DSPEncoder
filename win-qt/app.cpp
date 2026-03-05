/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * app.cpp — Application class implementation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "app.h"
#include "main_window.h"
#include "audio_pipeline.h"

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <QFile>
#include <QSettings>
#include <QStyleFactory>

namespace mc1 {

App::App(int &argc, char **argv)
    : QApplication(argc, argv)
{
#ifdef _WIN32
    /* Initialize Winsock2 before any socket operations */
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    setApplicationName(QStringLiteral("Mcaster1 DSP Encoder"));
    setApplicationVersion(versionString());
    setOrganizationName(QStringLiteral("Mcaster1"));
    setOrganizationDomain(QStringLiteral("mcaster1.com"));

    /* Phase M3: Create global audio pipeline */
    pipeline_ = new AudioPipeline;
    g_pipeline = pipeline_;

    /* Restore theme from QSettings before creating main window */
    QSettings s;
    int theme_idx = s.value(QStringLiteral("prefs/theme"), 0).toInt();
    if (theme_idx == 1)
        applyBrandedTheme();
    else
        applyNativeTheme();
    theme_ = (theme_idx == 1) ? Theme::Branded : Theme::Native;

    main_window_ = new MainWindow;
    main_window_->show();
}

App::~App()
{
    delete main_window_;
    delete pipeline_;
    g_pipeline = nullptr;
#ifdef _WIN32
    WSACleanup();
#endif
}

void App::setTheme(Theme t)
{
    theme_ = t;
    if (t == Theme::Native)
        applyNativeTheme();
    else
        applyBrandedTheme();
    emit themeChanged();
}

void App::applyNativeTheme()
{
    setStyleSheet(QString());
#ifdef _WIN32
    setStyle(QStyleFactory::create(QStringLiteral("windowsvista")));
#else
    setStyle(QStyleFactory::create(QStringLiteral("macOS")));
#endif
}

void App::applyBrandedTheme()
{
    QFile qss(QStringLiteral(":/styles/dark_theme.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStyleSheet(QString::fromUtf8(qss.readAll()));
    }
}

} // namespace mc1
