/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * log_viewer_dialog.h — Live log viewer with color-coded entries
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_LOG_VIEWER_DIALOG_H
#define MC1_LOG_VIEWER_DIALOG_H

#include <QDialog>
#include <QVector>
#include <QPair>

class QComboBox;
class QPushButton;
class QTextEdit;

namespace mc1 {

class LogViewerDialog : public QDialog {
    Q_OBJECT

public:
    explicit LogViewerDialog(QWidget *parent = nullptr);

    enum LogLevel { CRITICAL = 1, ERROR = 2, WARN = 3, INFO = 4, DEBUG = 5 };

public Q_SLOTS:
    void appendLine(int level, const QString &msg);

private Q_SLOTS:
    void onFilterChanged(int index);
    void onClear();
    void onCopy();

private:
    void rebuildView();
    static QString colorForLevel(int level);
    static QString labelForLevel(int level);

    QTextEdit   *text_edit_   = nullptr;
    QComboBox   *combo_filter_ = nullptr;
    QPushButton *btn_clear_   = nullptr;
    QPushButton *btn_copy_    = nullptr;

    static constexpr int MAX_LINES = 5000;
    QVector<QPair<int, QString>> ring_buffer_;
    int min_level_ = 1;  /* show all by default */
};

} // namespace mc1

#endif // MC1_LOG_VIEWER_DIALOG_H
