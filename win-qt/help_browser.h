/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * help_browser.h — Embedded documentation browser
 *
 * QDialog with sidebar navigation + QTextBrowser content pane.
 * Loads HTML docs from docs/ directory next to the executable.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_HELP_BROWSER_H
#define MC1_HELP_BROWSER_H

#include <QDialog>

class QListWidget;
class QTextBrowser;
class QPushButton;

namespace mc1 {

class HelpBrowser : public QDialog {
    Q_OBJECT
public:
    explicit HelpBrowser(QWidget *parent = nullptr);

    /* Navigate to a specific anchor (e.g., "effects-rack") */
    void navigateTo(const QString &anchor);

private slots:
    void onTopicClicked(int row);
    void onOpenInBrowser();

private:
    void loadDocs();

    QListWidget  *topic_list_  = nullptr;
    QTextBrowser *browser_     = nullptr;
    QPushButton  *btn_browser_ = nullptr;
    QString       docs_path_;

    struct Topic {
        QString label;
        QString anchor;
    };
    QVector<Topic> topics_;
};

} // namespace mc1

#endif // MC1_HELP_BROWSER_H
