/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * yp_settings_tab.h — YP (Yellow Pages) Settings tab
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_YP_SETTINGS_TAB_H
#define MC1_YP_SETTINGS_TAB_H

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include "config_types.h"

namespace mc1 {

class YPSettingsTab : public QWidget {
    Q_OBJECT

public:
    explicit YPSettingsTab(QWidget *parent = nullptr);

    void loadFromConfig(const EncoderConfig &cfg);
    void saveToConfig(EncoderConfig &cfg) const;

private:
    QCheckBox *chk_public_;
    QLineEdit *edit_stream_name_;
    QLineEdit *edit_stream_desc_;
    QLineEdit *edit_stream_url_;
    QLineEdit *edit_stream_genre_;
    QLineEdit *edit_icq_;
    QLineEdit *edit_aim_;
    QLineEdit *edit_irc_;
};

} // namespace mc1

#endif // MC1_YP_SETTINGS_TAB_H
