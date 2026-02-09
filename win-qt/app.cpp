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
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>

namespace mc1 {

static const char kInstanceKey[] = "Mcaster1DSPEncoderInstance";

bool App::isAlreadyRunning()
{
    /* Try to connect to an existing server. If successful, another instance is up. */
    QLocalSocket probe;
    probe.connectToServer(QString::fromLatin1(kInstanceKey));
    if (probe.waitForConnected(300)) {
        probe.write("show\n");
        probe.flush();
        probe.waitForBytesWritten(300);
        probe.disconnectFromServer();
        return true;
    }
    /* No server found — become the server for future instances */
    instance_server_ = new QLocalServer(this);
    QLocalServer::removeServer(QString::fromLatin1(kInstanceKey)); /* clean stale socket */
    instance_server_->listen(QString::fromLatin1(kInstanceKey));
    connect(instance_server_, &QLocalServer::newConnection, this, [this]() {
        QLocalSocket *sock = instance_server_->nextPendingConnection();
        connect(sock, &QLocalSocket::readyRead, this, [this, sock]() {
            QString msg = QString::fromUtf8(sock->readAll()).trimmed();
            if (msg == QStringLiteral("show") && main_window_) {
                main_window_->show();
                main_window_->raise();
                main_window_->activateWindow();
            }
            sock->deleteLater();
        });
    });
    return false;
}

void App::bringExistingToFront()
{
    /* isAlreadyRunning() already sent the "show" message; nothing more needed. */
}

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
    setWindowIcon(QIcon(QStringLiteral(":/icons/app-icon.svg")));

    /* Single-instance check — must happen before creating any windows */
    if (isAlreadyRunning()) {
        is_duplicate_ = true;
        QMessageBox::information(
            nullptr,
            QStringLiteral("Mcaster1 DSP Encoder — Already Running"),
            QStringLiteral("An instance of Mcaster1 DSP Encoder is already running.\n\n"
                           "The existing instance has been brought to the foreground."),
            QMessageBox::Ok);
        /* Schedule quit after event loop starts */
        QTimer::singleShot(0, this, &QApplication::quit);
        return;
    }

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
    if (!is_duplicate_) {
        if (instance_server_) {
            instance_server_->close();
            QLocalServer::removeServer(QString::fromLatin1(kInstanceKey));
        }
        delete main_window_;
        delete pipeline_;
        g_pipeline = nullptr;
    }
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
