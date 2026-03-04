/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * preferences_dialog.h — Preferences dialog (General + Paths)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_PREFERENCES_DIALOG_H
#define MC1_PREFERENCES_DIALOG_H

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace mc1 {

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

Q_SIGNALS:
    void themeChanged(int themeIndex);

private Q_SLOTS:
    void onApply();
    void onBrowsePlaylistDir();
    void onBrowseLogDir();
    void onBrowseArchiveDir();

private:
    void loadSettings();
    void saveSettings();

    /* General tab */
    QComboBox  *combo_theme_       = nullptr;
    QSpinBox   *spin_log_level_    = nullptr;
    QSpinBox   *spin_dnas_poll_    = nullptr;
    QCheckBox  *chk_auto_start_    = nullptr;
    QCheckBox  *chk_tray_close_    = nullptr;

    /* Paths tab */
    QLineEdit  *edit_playlist_dir_ = nullptr;
    QLineEdit  *edit_log_dir_      = nullptr;
    QLineEdit  *edit_archive_dir_  = nullptr;
};

} // namespace mc1

#endif // MC1_PREFERENCES_DIALOG_H
