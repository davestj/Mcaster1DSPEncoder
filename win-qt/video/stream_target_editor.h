/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/stream_target_editor.h — Stream target add/edit dialog
 *
 * Full dialog for adding or editing video stream targets.
 * Fields adapt based on server type (mount vs stream key, etc.).
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_STREAM_TARGET_EDITOR_H
#define MC1_STREAM_TARGET_EDITOR_H

#include <QDialog>
#include "live_video_studio.h"   /* StreamTargetEntry */

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace mc1 {

class StreamTargetEditor : public QDialog {
    Q_OBJECT

public:
    enum Mode { ADD, EDIT };
    enum Context { AUDIO, VIDEO };

    explicit StreamTargetEditor(Mode mode, QWidget *parent = nullptr,
                                Context ctx = AUDIO);

    /* Pre-populate for edit mode */
    void setTarget(const StreamTargetEntry &entry);

    /* Retrieve the result */
    StreamTargetEntry target() const;

private slots:
    void onServerTypeChanged(int index);
    void onTestConnection();

private:
    void buildUI();
    void updateFieldVisibility();

    Mode mode_;
    Context context_;

    QComboBox  *combo_server_type_ = nullptr;
    QLineEdit  *edit_host_         = nullptr;
    QSpinBox   *spin_port_         = nullptr;
    QLineEdit  *edit_mount_        = nullptr;
    QLineEdit  *edit_username_     = nullptr;
    QLineEdit  *edit_password_     = nullptr;
    QLineEdit  *edit_stream_key_  = nullptr;
    QLineEdit  *edit_output_dir_  = nullptr;
    QPushButton *btn_browse_dir_  = nullptr;
    QSpinBox   *spin_seg_dur_     = nullptr;
    QSpinBox   *spin_max_segs_    = nullptr;

    QLabel     *lbl_host_           = nullptr;
    QLabel     *lbl_port_           = nullptr;
    QLabel     *lbl_mount_         = nullptr;
    QLabel     *lbl_username_      = nullptr;
    QLabel     *lbl_password_      = nullptr;
    QLabel     *lbl_stream_key_    = nullptr;
    QLabel     *lbl_output_dir_    = nullptr;
    QLabel     *lbl_seg_dur_       = nullptr;
    QLabel     *lbl_max_segs_      = nullptr;

    QPushButton *btn_test_         = nullptr;
    QLabel      *lbl_test_result_  = nullptr;

    QPushButton *btn_ok_           = nullptr;
    QPushButton *btn_cancel_       = nullptr;
};

} // namespace mc1

#endif // MC1_STREAM_TARGET_EDITOR_H
