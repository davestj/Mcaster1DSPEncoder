/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * edit_metadata_dialog.cpp — Edit stream metadata dialog
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "edit_metadata_dialog.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QButtonGroup>

namespace mc1 {

EditMetadataDialog::EditMetadataDialog(MetadataConfig &cfg, QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowTitle(QStringLiteral("Set / Lock Metadata"));
    resize(440, 340);
    auto *top = new QVBoxLayout(this);

    /* ── Manual metadata ──────────────────────────────────────── */
    auto *manual_grp = new QGroupBox(QStringLiteral("Manually Enter Metadata"));
    auto *manual_form = new QFormLayout(manual_grp);
    manual_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    chk_lock_ = new QCheckBox(QStringLiteral("Lock Metadata (prevent auto-update)"));
    chk_lock_->setChecked(cfg_.lock_metadata);
    manual_form->addRow(QString(), chk_lock_);

    edit_manual_ = new QLineEdit(QString::fromStdString(cfg_.manual_text));
    manual_form->addRow(QStringLiteral("Metadata:"), edit_manual_);

    edit_append_ = new QLineEdit(QString::fromStdString(cfg_.append_string));
    manual_form->addRow(QStringLiteral("Append String:"), edit_append_);

    top->addWidget(manual_grp);

    /* ── External metadata source ─────────────────────────────── */
    auto *ext_grp = new QGroupBox(QStringLiteral("External Metadata Source"));
    auto *ext_form = new QFormLayout(ext_grp);
    ext_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    radio_url_      = new QRadioButton(QStringLiteral("URL"));
    radio_file_     = new QRadioButton(QStringLiteral("File"));
    radio_disabled_ = new QRadioButton(QStringLiteral("Disabled"));

    auto *radio_group = new QButtonGroup(this);
    radio_group->addButton(radio_url_);
    radio_group->addButton(radio_file_);
    radio_group->addButton(radio_disabled_);

    switch (cfg_.source) {
        case MetadataConfig::Source::URL:      radio_url_->setChecked(true); break;
        case MetadataConfig::Source::FILE:     radio_file_->setChecked(true); break;
        case MetadataConfig::Source::MANUAL:   radio_disabled_->setChecked(true); break;
        case MetadataConfig::Source::DISABLED: radio_disabled_->setChecked(true); break;
    }

    auto *radio_row = new QHBoxLayout;
    radio_row->addWidget(radio_url_);
    radio_row->addWidget(radio_file_);
    radio_row->addWidget(radio_disabled_);
    ext_form->addRow(QStringLiteral("Source:"), radio_row);

    edit_meta_url_ = new QLineEdit(QString::fromStdString(cfg_.meta_url));
    edit_meta_url_->setPlaceholderText(QStringLiteral("https://example.com/nowplaying.txt"));
    ext_form->addRow(QStringLiteral("Meta URL:"), edit_meta_url_);

    edit_meta_file_ = new QLineEdit(QString::fromStdString(cfg_.meta_file));
    edit_meta_file_->setPlaceholderText(QStringLiteral("/path/to/metadata.txt"));
    ext_form->addRow(QStringLiteral("Meta File:"), edit_meta_file_);

    spin_interval_ = new QSpinBox;
    spin_interval_->setRange(1, 600);
    spin_interval_->setValue(cfg_.update_interval_sec);
    spin_interval_->setSuffix(QStringLiteral(" sec"));
    ext_form->addRow(QStringLiteral("Interval:"), spin_interval_);

    top->addWidget(ext_grp);

    /* ── Buttons ──────────────────────────────────────────────── */
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    top->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    /* Signals */
    connect(radio_url_,      &QRadioButton::toggled, this, &EditMetadataDialog::onSourceChanged);
    connect(radio_file_,     &QRadioButton::toggled, this, &EditMetadataDialog::onSourceChanged);
    connect(radio_disabled_, &QRadioButton::toggled, this, &EditMetadataDialog::onSourceChanged);

    updateEnabled();
}

void EditMetadataDialog::onSourceChanged()
{
    updateEnabled();
}

void EditMetadataDialog::updateEnabled()
{
    bool url_mode  = radio_url_->isChecked();
    bool file_mode = radio_file_->isChecked();
    bool disabled  = radio_disabled_->isChecked();

    edit_meta_url_->setEnabled(url_mode);
    edit_meta_file_->setEnabled(file_mode);
    spin_interval_->setEnabled(url_mode || file_mode);
    edit_manual_->setEnabled(disabled);
    chk_lock_->setEnabled(disabled);
}

void EditMetadataDialog::accept()
{
    cfg_.lock_metadata      = chk_lock_->isChecked();
    cfg_.manual_text        = edit_manual_->text().toStdString();
    cfg_.append_string      = edit_append_->text().toStdString();
    cfg_.meta_url           = edit_meta_url_->text().toStdString();
    cfg_.meta_file          = edit_meta_file_->text().toStdString();
    cfg_.update_interval_sec = spin_interval_->value();

    if (radio_url_->isChecked())       cfg_.source = MetadataConfig::Source::URL;
    else if (radio_file_->isChecked()) cfg_.source = MetadataConfig::Source::FILE;
    else                               cfg_.source = MetadataConfig::Source::DISABLED;

    QDialog::accept();
}

} // namespace mc1
