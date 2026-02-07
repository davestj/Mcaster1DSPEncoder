/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * about_dialog.h — About dialog
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_ABOUT_DIALOG_H
#define MC1_ABOUT_DIALOG_H

#include <QDialog>

namespace mc1 {

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

} // namespace mc1

#endif // MC1_ABOUT_DIALOG_H
