/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * preferences_dialog.cpp — Preferences dialog implementation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "preferences_dialog.h"
#include "app.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace mc1 {

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Preferences"));
    setMinimumSize(460, 340);
    resize(500, 380);

    auto *root = new QVBoxLayout(this);

    auto *tabs = new QTabWidget;

    /* ── General Tab ── */
    auto *gen_page = new QWidget;
    auto *gen_form = new QFormLayout(gen_page);
    gen_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    combo_theme_ = new QComboBox;
    combo_theme_->addItem(QStringLiteral("Native (macOS)"));
    combo_theme_->addItem(QStringLiteral("Branded Dark"));
    gen_form->addRow(QStringLiteral("Theme:"), combo_theme_);

    spin_log_level_ = new QSpinBox;
    spin_log_level_->setRange(1, 5);
    spin_log_level_->setValue(4);
    spin_log_level_->setToolTip(QStringLiteral(
        "1 = Critical only, 2 = Errors, 3 = Warnings, 4 = Info, 5 = Debug"));
    gen_form->addRow(QStringLiteral("Log Level:"), spin_log_level_);

    spin_dnas_poll_ = new QSpinBox;
    spin_dnas_poll_->setRange(5, 120);
    spin_dnas_poll_->setValue(15);
    spin_dnas_poll_->setSuffix(QStringLiteral(" sec"));
    spin_dnas_poll_->setToolTip(QStringLiteral(
        "How often to poll DNAS/Icecast servers for listener stats"));
    gen_form->addRow(QStringLiteral("DNAS Poll Interval:"), spin_dnas_poll_);

    chk_auto_start_ = new QCheckBox(QStringLiteral("Auto-start encoder slots on launch"));
    gen_form->addRow(QString(), chk_auto_start_);

    chk_tray_close_ = new QCheckBox(QStringLiteral("Minimize to system tray on close"));
    chk_tray_close_->setChecked(true);
    gen_form->addRow(QString(), chk_tray_close_);

    tabs->addTab(gen_page, QStringLiteral("General"));

    /* ── Paths Tab ── */
    auto *path_page = new QWidget;
    auto *path_form = new QFormLayout(path_page);
    path_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto makeBrowseRow = [this, path_form](const QString &label,
                                            QLineEdit *&edit,
                                            const char *slot_method) {
        auto *row = new QHBoxLayout;
        edit = new QLineEdit;
        edit->setPlaceholderText(QStringLiteral("Default"));
        row->addWidget(edit, 1);
        auto *btn = new QPushButton(QStringLiteral("Browse..."));
        row->addWidget(btn);
        path_form->addRow(label, row);
        return btn;
    };

    auto *btn_pl = makeBrowseRow(QStringLiteral("Playlist Dir:"),
                                  edit_playlist_dir_, nullptr);
    connect(btn_pl, &QPushButton::clicked,
            this, &PreferencesDialog::onBrowsePlaylistDir);

    auto *btn_log = makeBrowseRow(QStringLiteral("Log Dir:"),
                                   edit_log_dir_, nullptr);
    connect(btn_log, &QPushButton::clicked,
            this, &PreferencesDialog::onBrowseLogDir);

    auto *btn_arch = makeBrowseRow(QStringLiteral("Archive Dir:"),
                                    edit_archive_dir_, nullptr);
    connect(btn_arch, &QPushButton::clicked,
            this, &PreferencesDialog::onBrowseArchiveDir);

    tabs->addTab(path_page, QStringLiteral("Paths"));

    root->addWidget(tabs);

    /* ── Buttons ── */
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        onApply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &PreferencesDialog::onApply);
    root->addWidget(buttons);

    loadSettings();
}

void PreferencesDialog::loadSettings()
{
    QSettings s;
    combo_theme_->setCurrentIndex(
        s.value(QStringLiteral("prefs/theme"), 0).toInt());
    spin_log_level_->setValue(
        s.value(QStringLiteral("prefs/logLevel"), 4).toInt());
    spin_dnas_poll_->setValue(
        s.value(QStringLiteral("prefs/dnasPollSec"), 15).toInt());
    chk_auto_start_->setChecked(
        s.value(QStringLiteral("prefs/autoStart"), false).toBool());
    chk_tray_close_->setChecked(
        s.value(QStringLiteral("prefs/trayOnClose"), true).toBool());
    edit_playlist_dir_->setText(
        s.value(QStringLiteral("prefs/playlistDir")).toString());
    edit_log_dir_->setText(
        s.value(QStringLiteral("prefs/logDir")).toString());
    edit_archive_dir_->setText(
        s.value(QStringLiteral("prefs/archiveDir")).toString());
}

void PreferencesDialog::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("prefs/theme"),       combo_theme_->currentIndex());
    s.setValue(QStringLiteral("prefs/logLevel"),     spin_log_level_->value());
    s.setValue(QStringLiteral("prefs/dnasPollSec"),  spin_dnas_poll_->value());
    s.setValue(QStringLiteral("prefs/autoStart"),    chk_auto_start_->isChecked());
    s.setValue(QStringLiteral("prefs/trayOnClose"),  chk_tray_close_->isChecked());
    s.setValue(QStringLiteral("prefs/playlistDir"),  edit_playlist_dir_->text());
    s.setValue(QStringLiteral("prefs/logDir"),        edit_log_dir_->text());
    s.setValue(QStringLiteral("prefs/archiveDir"),    edit_archive_dir_->text());
}

void PreferencesDialog::onApply()
{
    saveSettings();

    /* Apply theme immediately */
    int idx = combo_theme_->currentIndex();
    auto *app = App::instance();
    app->setTheme(idx == 0 ? App::Theme::Native : App::Theme::Branded);
    Q_EMIT themeChanged(idx);
}

void PreferencesDialog::onBrowsePlaylistDir()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Playlist Directory"),
        edit_playlist_dir_->text());
    if (!dir.isEmpty()) edit_playlist_dir_->setText(dir);
}

void PreferencesDialog::onBrowseLogDir()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Log Directory"),
        edit_log_dir_->text());
    if (!dir.isEmpty()) edit_log_dir_->setText(dir);
}

void PreferencesDialog::onBrowseArchiveDir()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Archive Directory"),
        edit_archive_dir_->text());
    if (!dir.isEmpty()) edit_archive_dir_->setText(dir);
}

} // namespace mc1
