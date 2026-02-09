/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/manage_devices_dialog.cpp — Virtual Camera device management
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "manage_devices_dialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace mc1 {

ManageDevicesDialog::ManageDevicesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Manage Virtual Camera"));
    setMinimumWidth(460);
    buildUI();
    refreshStatus();
}

void ManageDevicesDialog::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(8);

    /* ── Status group ── */
    auto *status_group = new QGroupBox(QStringLiteral("Virtual Camera Status"));
    auto *status_lay   = new QFormLayout(status_group);
    status_lay->setSpacing(4);

    lbl_status_ = new QLabel;
    lbl_status_->setTextFormat(Qt::RichText);
    status_lay->addRow(QStringLiteral("Status:"), lbl_status_);

    lbl_dll_path_ = new QLabel;
    lbl_dll_path_->setWordWrap(true);
    lbl_dll_path_->setStyleSheet(QStringLiteral("color: #99aabb; font-size: 10px;"));
    status_lay->addRow(QStringLiteral("DLL Path:"), lbl_dll_path_);

    lbl_elevated_ = new QLabel;
    status_lay->addRow(QStringLiteral("Privileges:"), lbl_elevated_);

    root->addWidget(status_group);

    /* ── Configuration group ── */
    auto *config_group = new QGroupBox(QStringLiteral("Configuration"));
    auto *config_lay   = new QFormLayout(config_group);
    config_lay->setSpacing(4);

    edit_name_ = new QLineEdit(QStringLiteral("Mcaster1 Virtual Camera"));
    config_lay->addRow(QStringLiteral("Device Name:"), edit_name_);

    cmb_resolution_ = new QComboBox;
    cmb_resolution_->addItem(QStringLiteral("480p  (854x480)"),  QStringLiteral("854x480"));
    cmb_resolution_->addItem(QStringLiteral("720p  (1280x720)"), QStringLiteral("1280x720"));
    cmb_resolution_->addItem(QStringLiteral("1080p (1920x1080)"), QStringLiteral("1920x1080"));
    cmb_resolution_->setCurrentIndex(1);
    config_lay->addRow(QStringLiteral("Resolution:"), cmb_resolution_);

    cmb_fps_ = new QComboBox;
    cmb_fps_->addItems({QStringLiteral("15"), QStringLiteral("24"),
                        QStringLiteral("25"), QStringLiteral("30"),
                        QStringLiteral("60")});
    cmb_fps_->setCurrentIndex(3); /* 30 fps */
    config_lay->addRow(QStringLiteral("FPS:"), cmb_fps_);

    root->addWidget(config_group);

    /* ── Action buttons ── */
    auto *btn_row = new QHBoxLayout;
    btn_row->setSpacing(8);

    btn_register_ = new QPushButton(QStringLiteral("Register Virtual Camera"));
    btn_register_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 8px 16px; "
        "border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #00e8bb; }"
        "QPushButton:disabled { background: #444; color: #666; }"));
    connect(btn_register_, &QPushButton::clicked, this, &ManageDevicesDialog::onRegister);
    btn_row->addWidget(btn_register_);

    btn_unregister_ = new QPushButton(QStringLiteral("Unregister"));
    btn_unregister_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #aa2233; color: white; padding: 8px 16px; "
        "border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #cc3344; }"
        "QPushButton:disabled { background: #444; color: #666; }"));
    connect(btn_unregister_, &QPushButton::clicked, this, &ManageDevicesDialog::onUnregister);
    btn_row->addWidget(btn_unregister_);

    btn_row->addStretch();

    auto *btn_close = new QPushButton(QStringLiteral("Close"));
    connect(btn_close, &QPushButton::clicked, this, &QDialog::accept);
    btn_row->addWidget(btn_close);

    root->addLayout(btn_row);

    /* ── Info note ── */
    auto *note = new QLabel(QStringLiteral(
        "Registration requires Administrator privileges. "
        "The virtual camera DLL (Mcaster1VirtualCam.dll) must be in the "
        "same directory as the encoder application."));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral(
        "color: #667788; font-size: 10px; padding: 4px;"));
    root->addWidget(note);
}

bool ManageDevicesDialog::isVCamRegistered() const
{
#ifdef _WIN32
    /* Check if our CLSID exists in the registry */
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(
        HKEY_CLASSES_ROOT,
        L"CLSID\\{F7D3A1B2-C4E5-4F6D-8A9B-0C1D2E3F4A5B}\\InprocServer32",
        0, KEY_READ, &hKey);
    if (rc == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
#endif
    return false;
}

bool ManageDevicesDialog::isElevated() const
{
#ifdef _WIN32
    BOOL elevated = FALSE;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION te = {};
        DWORD size = sizeof(te);
        if (GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &size)) {
            elevated = te.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return elevated != FALSE;
#else
    return false;
#endif
}

QString ManageDevicesDialog::findVCamDll() const
{
    /* Look for the DLL next to the application exe */
    QString appDir = QCoreApplication::applicationDirPath();
    QString dllPath = appDir + QStringLiteral("/Mcaster1VirtualCam.dll");
    if (QFileInfo::exists(dllPath))
        return dllPath;
    return {};
}

void ManageDevicesDialog::refreshStatus()
{
    bool registered = isVCamRegistered();
    bool elevated = isElevated();
    QString dllPath = findVCamDll();

    if (registered) {
        lbl_status_->setText(QStringLiteral(
            "<span style='color:#00d4aa; font-weight:bold;'>REGISTERED</span>"));
    } else {
        lbl_status_->setText(QStringLiteral(
            "<span style='color:#ff8800; font-weight:bold;'>NOT REGISTERED</span>"));
    }

    if (dllPath.isEmpty()) {
        lbl_dll_path_->setText(QStringLiteral(
            "Mcaster1VirtualCam.dll NOT FOUND — build and place next to exe"));
        lbl_dll_path_->setStyleSheet(QStringLiteral("color: #ff4444; font-size: 10px;"));
    } else {
        lbl_dll_path_->setText(dllPath);
        lbl_dll_path_->setStyleSheet(QStringLiteral("color: #99aabb; font-size: 10px;"));
    }

    if (elevated) {
        lbl_elevated_->setText(QStringLiteral(
            "<span style='color:#00d4aa;'>Administrator</span>"));
    } else {
        lbl_elevated_->setText(QStringLiteral(
            "<span style='color:#ff8800;'>Standard User</span> "
            "(will prompt for elevation)"));
    }

    btn_register_->setEnabled(!registered && !dllPath.isEmpty());
    btn_unregister_->setEnabled(registered);
}

void ManageDevicesDialog::onRegister()
{
    QString dllPath = findVCamDll();
    if (dllPath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("DLL Not Found"),
            QStringLiteral("Mcaster1VirtualCam.dll was not found next to the "
                           "application. Build the virtual camera DLL first."));
        return;
    }

#ifdef _WIN32
    /* Use regsvr32 to register the DLL.
     * If we're not elevated, ShellExecuteEx with "runas" verb will
     * trigger a UAC prompt for elevation. */
    QString regsvr32 = QStringLiteral("regsvr32");
    QString args = QStringLiteral("/s \"%1\"").arg(
        QDir::toNativeSeparators(dllPath));

    if (isElevated()) {
        /* Already elevated — run directly */
        QProcess proc;
        proc.start(regsvr32, QStringList{QStringLiteral("/s"), dllPath});
        proc.waitForFinished(10000);
        int exitCode = proc.exitCode();
        if (exitCode == 0) {
            QMessageBox::information(this, QStringLiteral("Success"),
                QStringLiteral("Mcaster1 Virtual Camera registered successfully.\n"
                               "It will appear as a camera device in other applications."));
        } else {
            QMessageBox::warning(this, QStringLiteral("Registration Failed"),
                QString("regsvr32 exited with code %1").arg(exitCode));
        }
    } else {
        /* Request elevation via ShellExecuteEx */
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = L"regsvr32";

        /* Build args as wide string */
        std::wstring wargs = L"/s \"" +
            QDir::toNativeSeparators(dllPath).toStdWString() + L"\"";
        sei.lpParameters = wargs.c_str();
        sei.nShow = SW_HIDE;

        if (ShellExecuteExW(&sei)) {
            WaitForSingleObject(sei.hProcess, 15000);
            DWORD exitCode = 0;
            GetExitCodeProcess(sei.hProcess, &exitCode);
            CloseHandle(sei.hProcess);

            if (exitCode == 0) {
                QMessageBox::information(this, QStringLiteral("Success"),
                    QStringLiteral("Mcaster1 Virtual Camera registered successfully."));
            } else {
                QMessageBox::warning(this, QStringLiteral("Registration Failed"),
                    QString("regsvr32 exited with code %1").arg(exitCode));
            }
        } else {
            QMessageBox::warning(this, QStringLiteral("Elevation Denied"),
                QStringLiteral("Administrator privileges are required to register "
                               "the virtual camera. Please approve the UAC prompt."));
        }
    }
#endif

    refreshStatus();
}

void ManageDevicesDialog::onUnregister()
{
    QString dllPath = findVCamDll();
    if (dllPath.isEmpty()) {
        /* DLL gone but still registered — try to unregister by CLSID directly */
    }

    int ret = QMessageBox::question(this, QStringLiteral("Confirm Unregister"),
        QStringLiteral("Remove the Mcaster1 Virtual Camera from the system?\n"
                       "Other applications will no longer see it as a camera device."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

#ifdef _WIN32
    if (isElevated()) {
        QProcess proc;
        proc.start(QStringLiteral("regsvr32"),
                   QStringList{QStringLiteral("/s"), QStringLiteral("/u"), dllPath});
        proc.waitForFinished(10000);
    } else {
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = L"regsvr32";

        std::wstring wargs = L"/s /u \"" +
            QDir::toNativeSeparators(dllPath).toStdWString() + L"\"";
        sei.lpParameters = wargs.c_str();
        sei.nShow = SW_HIDE;

        if (ShellExecuteExW(&sei)) {
            WaitForSingleObject(sei.hProcess, 15000);
            CloseHandle(sei.hProcess);
        }
    }
#endif

    refreshStatus();
}

} // namespace mc1
