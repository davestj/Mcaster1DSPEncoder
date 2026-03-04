/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * log_viewer_dialog.cpp — Live log viewer implementation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "log_viewer_dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QDateTime>

namespace mc1 {

LogViewerDialog::LogViewerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Log Viewer"));
    setMinimumSize(640, 400);
    resize(780, 520);
    setAttribute(Qt::WA_DeleteOnClose, false); /* keep alive when hidden */

    auto *root = new QVBoxLayout(this);

    /* Toolbar row */
    auto *toolbar = new QHBoxLayout;

    toolbar->addWidget(new QLabel(QStringLiteral("Level:")));
    combo_filter_ = new QComboBox;
    combo_filter_->addItem(QStringLiteral("All (1+)"));
    combo_filter_->addItem(QStringLiteral("Critical (1)"));
    combo_filter_->addItem(QStringLiteral("Error (2+)"));
    combo_filter_->addItem(QStringLiteral("Warn (3+)"));
    combo_filter_->addItem(QStringLiteral("Info (4+)"));
    combo_filter_->addItem(QStringLiteral("Debug (5)"));
    connect(combo_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogViewerDialog::onFilterChanged);
    toolbar->addWidget(combo_filter_);

    toolbar->addStretch();

    btn_copy_ = new QPushButton(QStringLiteral("Copy All"));
    connect(btn_copy_, &QPushButton::clicked, this, &LogViewerDialog::onCopy);
    toolbar->addWidget(btn_copy_);

    btn_clear_ = new QPushButton(QStringLiteral("Clear"));
    connect(btn_clear_, &QPushButton::clicked, this, &LogViewerDialog::onClear);
    toolbar->addWidget(btn_clear_);

    root->addLayout(toolbar);

    /* Log text area */
    text_edit_ = new QTextEdit;
    text_edit_->setReadOnly(true);
    text_edit_->setFont(QFont(QStringLiteral("Menlo"), 11));
    text_edit_->setLineWrapMode(QTextEdit::NoWrap);
    root->addWidget(text_edit_);

    /* Seed some startup messages */
    appendLine(INFO, QStringLiteral("Log viewer initialized"));
}

void LogViewerDialog::appendLine(int level, const QString &msg)
{
    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString entry = QString("[%1] [%2] %3")
        .arg(ts, labelForLevel(level), msg);

    ring_buffer_.append({level, entry});
    if (ring_buffer_.size() > MAX_LINES)
        ring_buffer_.removeFirst();

    /* Only display if this level passes the filter */
    if (level <= min_level_ || min_level_ == 0) {
        QString color = colorForLevel(level);
        text_edit_->append(
            QStringLiteral("<span style='color:%1'>%2</span>")
                .arg(color, entry.toHtmlEscaped()));

        /* Auto-scroll to bottom */
        auto *sb = text_edit_->verticalScrollBar();
        if (sb) sb->setValue(sb->maximum());
    }
}

void LogViewerDialog::onFilterChanged(int index)
{
    /* 0=All, 1=Critical(1), 2=Error(2), 3=Warn(3), 4=Info(4), 5=Debug(5) */
    switch (index) {
    case 0: min_level_ = 5; break; /* show all */
    case 1: min_level_ = 1; break;
    case 2: min_level_ = 2; break;
    case 3: min_level_ = 3; break;
    case 4: min_level_ = 4; break;
    case 5: min_level_ = 5; break;
    default: min_level_ = 5; break;
    }
    rebuildView();
}

void LogViewerDialog::onClear()
{
    ring_buffer_.clear();
    text_edit_->clear();
}

void LogViewerDialog::onCopy()
{
    QApplication::clipboard()->setText(text_edit_->toPlainText());
}

void LogViewerDialog::rebuildView()
{
    text_edit_->clear();
    for (const auto &[level, entry] : ring_buffer_) {
        if (level <= min_level_) {
            QString color = colorForLevel(level);
            text_edit_->append(
                QStringLiteral("<span style='color:%1'>%2</span>")
                    .arg(color, entry.toHtmlEscaped()));
        }
    }
}

QString LogViewerDialog::colorForLevel(int level)
{
    switch (level) {
    case CRITICAL: return QStringLiteral("#ff4444");
    case ERROR:    return QStringLiteral("#ff8800");
    case WARN:     return QStringLiteral("#ddcc00");
    case INFO:     return QStringLiteral("#cccccc");
    case DEBUG:    return QStringLiteral("#888888");
    default:       return QStringLiteral("#cccccc");
    }
}

QString LogViewerDialog::labelForLevel(int level)
{
    switch (level) {
    case CRITICAL: return QStringLiteral("CRIT");
    case ERROR:    return QStringLiteral("ERR ");
    case WARN:     return QStringLiteral("WARN");
    case INFO:     return QStringLiteral("INFO");
    case DEBUG:    return QStringLiteral("DBG ");
    default:       return QStringLiteral("????");
    }
}

} // namespace mc1
