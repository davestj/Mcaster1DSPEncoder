/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * edit_metadata_dialog.h — Edit stream metadata dialog
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_EDIT_METADATA_DIALOG_H
#define MC1_EDIT_METADATA_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QSpinBox>
#include "config_types.h"

namespace mc1 {

class EditMetadataDialog : public QDialog {
    Q_OBJECT

public:
    explicit EditMetadataDialog(MetadataConfig &cfg, QWidget *parent = nullptr);

private Q_SLOTS:
    void onSourceChanged();

private:
    void accept() override;
    void updateEnabled();

    MetadataConfig &cfg_;
    QCheckBox    *chk_lock_;
    QLineEdit    *edit_manual_;
    QLineEdit    *edit_append_;
    QRadioButton *radio_url_;
    QRadioButton *radio_file_;
    QRadioButton *radio_disabled_;
    QLineEdit    *edit_meta_url_;
    QLineEdit    *edit_meta_file_;
    QSpinBox     *spin_interval_;
};

} // namespace mc1

#endif // MC1_EDIT_METADATA_DIALOG_H
