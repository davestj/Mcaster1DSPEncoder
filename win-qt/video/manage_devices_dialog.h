/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/manage_devices_dialog.h — Virtual Camera device management dialog
 *
 * Allows users to:
 *   - Register/unregister the Mcaster1 Virtual Camera DirectShow filter
 *   - Set the virtual camera name
 *   - View registration status
 *   - Configure output resolution and FPS
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_MANAGE_DEVICES_DIALOG_H
#define MC1_MANAGE_DEVICES_DIALOG_H

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace mc1 {

class ManageDevicesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ManageDevicesDialog(QWidget *parent = nullptr);

private slots:
    void onRegister();
    void onUnregister();
    void refreshStatus();

private:
    void buildUI();
    bool isVCamRegistered() const;
    bool isElevated() const;
    QString findVCamDll() const;

    QLabel      *lbl_status_     = nullptr;
    QLabel      *lbl_dll_path_   = nullptr;
    QLabel      *lbl_elevated_   = nullptr;
    QLineEdit   *edit_name_      = nullptr;
    QComboBox   *cmb_resolution_ = nullptr;
    QComboBox   *cmb_fps_        = nullptr;
    QPushButton *btn_register_   = nullptr;
    QPushButton *btn_unregister_ = nullptr;
};

} // namespace mc1

#endif // MC1_MANAGE_DEVICES_DIALOG_H
