/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * help_browser.cpp — Embedded documentation browser
 *
 * Sidebar topic list + QTextBrowser for HTML docs with inline screenshots.
 * Falls back to system browser if docs/ directory is missing.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "help_browser.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace mc1 {

HelpBrowser::HelpBrowser(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Mcaster1 DSP Encoder — Documentation"));
    resize(980, 700);
    setMinimumSize(700, 500);

    docs_path_ = QCoreApplication::applicationDirPath() + QStringLiteral("/docs");

    /* Define topics matching index.html anchors */
    topics_ = {
        { QStringLiteral("Overview"),             QStringLiteral("overview") },
        { QStringLiteral("Getting Started"),      QStringLiteral("getting-started") },
        { QStringLiteral("---"),                  QString() },
        { QStringLiteral("Main Window"),          QStringLiteral("main-window") },
        { QStringLiteral("Encoder Types"),        QStringLiteral("encoder-types") },
        { QStringLiteral("Encoder Configuration"),QStringLiteral("encoder-config") },
        { QStringLiteral("---"),                  QString() },
        { QStringLiteral("Effects Rack"),         QStringLiteral("effects-rack") },
        { QStringLiteral("10-Band Parametric EQ"),QStringLiteral("parametric-eq") },
        { QStringLiteral("31-Band Graphic EQ"),   QStringLiteral("graphic-eq") },
        { QStringLiteral("AGC / Compressor"),     QStringLiteral("agc-compressor") },
        { QStringLiteral("Sonic Enhancer"),       QStringLiteral("sonic-enhancer") },
        { QStringLiteral("DBX Voice Processor"),  QStringLiteral("dbx-voice") },
        { QStringLiteral("---"),                  QString() },
        { QStringLiteral("Live Video Studio"),    QStringLiteral("video-studio") },
        { QStringLiteral("Video Overlays"),       QStringLiteral("video-overlay") },
        { QStringLiteral("---"),                  QString() },
        { QStringLiteral("Preview Audio Studio"), QStringLiteral("preview-audio") },
        { QStringLiteral("Streaming Setup"),      QStringLiteral("streaming") },
        { QStringLiteral("---"),                  QString() },
        { QStringLiteral("Mcaster1 Ecosystem"),   QStringLiteral("resources") },
        { QStringLiteral("Keyboard Shortcuts"),   QStringLiteral("keyboard-shortcuts") },
    };

    auto *main_lay = new QVBoxLayout(this);
    main_lay->setContentsMargins(0, 0, 0, 0);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    /* Sidebar */
    auto *sidebar = new QWidget;
    auto *side_lay = new QVBoxLayout(sidebar);
    side_lay->setContentsMargins(8, 12, 4, 8);

    auto *brand = new QLabel(QStringLiteral(
        "<h3 style='color:#00d4aa; margin:0;'>Mcaster1 DSP Encoder</h3>"
        "<small style='color:#94a3b8;'>Documentation</small>"));
    side_lay->addWidget(brand);

    topic_list_ = new QListWidget;
    topic_list_->setFocusPolicy(Qt::NoFocus);
    topic_list_->setStyleSheet(QStringLiteral(
        "QListWidget { background: #0d1320; border: none; color: #94a3b8; "
        "font-size: 13px; }"
        "QListWidget::item { padding: 6px 12px; border: none; }"
        "QListWidget::item:selected { background: rgba(0,212,170,0.12); "
        "color: #00d4aa; border-left: 3px solid #00d4aa; }"
        "QListWidget::item:hover { background: rgba(0,212,170,0.06); }"));

    for (const auto &t : topics_) {
        if (t.label == QStringLiteral("---")) {
            auto *item = new QListWidgetItem(QString());
            item->setFlags(Qt::NoItemFlags);
            item->setSizeHint(QSize(0, 8));
            topic_list_->addItem(item);
        } else {
            topic_list_->addItem(t.label);
        }
    }

    connect(topic_list_, &QListWidget::currentRowChanged,
            this, &HelpBrowser::onTopicClicked);
    side_lay->addWidget(topic_list_, 1);

    btn_browser_ = new QPushButton(QStringLiteral("Open in Browser"));
    btn_browser_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a3a5c; color: #d0e8f8; padding: 6px; "
        "border: 1px solid #2a5a8c; border-radius: 4px; }"
        "QPushButton:hover { background: #2a5a8c; }"));
    connect(btn_browser_, &QPushButton::clicked, this, &HelpBrowser::onOpenInBrowser);
    side_lay->addWidget(btn_browser_);

    sidebar->setFixedWidth(220);
    sidebar->setStyleSheet(QStringLiteral(
        "QWidget { background: #0d1320; }"));

    /* Content browser */
    browser_ = new QTextBrowser;
    browser_->setOpenExternalLinks(true);
    browser_->setStyleSheet(QStringLiteral(
        "QTextBrowser { background: #0a0e1a; color: #e2e8f0; border: none; "
        "padding: 20px 32px; font-size: 14px; }"
        "QTextBrowser a { color: #38bdf8; }"));

    splitter->addWidget(sidebar);
    splitter->addWidget(browser_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    main_lay->addWidget(splitter);

    /* Load docs */
    loadDocs();

    if (topic_list_->count() > 0)
        topic_list_->setCurrentRow(0);
}

void HelpBrowser::loadDocs()
{
    /* Set search paths so QTextBrowser can resolve relative image paths */
    QStringList paths;
    paths << docs_path_;
    paths << docs_path_ + QStringLiteral("/screenshots");
    browser_->setSearchPaths(paths);

    QString index = docs_path_ + QStringLiteral("/index.html");
    if (QFile::exists(index)) {
        QFile f(index);
        if (f.open(QIODevice::ReadOnly)) {
            QString html = QString::fromUtf8(f.readAll());

            /* QTextBrowser doesn't support <link> CSS, <aside>, <main>, <nav>,
             * or CSS3 grid/flexbox. Strip those and inject inline styles for
             * basic readability in the embedded viewer. */
            html.replace(QStringLiteral("<aside class=\"sidebar\">"),
                         QStringLiteral("<div style='display:none;'>"));
            html.replace(QStringLiteral("</aside>"),
                         QStringLiteral("</div>"));
            html.replace(QStringLiteral("<main class=\"content\">"),
                         QStringLiteral("<div>"));
            html.replace(QStringLiteral("</main>"),
                         QStringLiteral("</div>"));
            html.replace(QStringLiteral("<div class=\"page-wrapper\">"),
                         QStringLiteral("<div>"));
            html.replace(QStringLiteral("class=\"eco-card\""),
                         QStringLiteral("style='margin:8px 0; padding:8px; "
                                        "border:1px solid #1e3a5f; border-radius:4px;'"));
            html.replace(QStringLiteral("class=\"feature-card\""),
                         QStringLiteral("style='margin:16px 0; padding:16px; "
                                        "background:#1a2235; border:1px solid #1e3a5f; "
                                        "border-radius:6px;'"));

            browser_->setHtml(html);
        }
    } else {
        browser_->setHtml(QStringLiteral(
            "<h2 style='color:#00d4aa;'>Documentation Not Found</h2>"
            "<p>The docs/ directory was not found next to the executable.</p>"
            "<p>Click <b>Open in Browser</b> to view online documentation at "
            "<a href='https://mcaster1.com/encoder.php'>mcaster1.com/encoder.php</a></p>"));
    }
}

void HelpBrowser::navigateTo(const QString &anchor)
{
    if (!anchor.isEmpty())
        browser_->scrollToAnchor(anchor);
}

void HelpBrowser::onTopicClicked(int row)
{
    if (row < 0 || row >= topics_.size()) return;
    const auto &topic = topics_[row];
    if (topic.anchor.isEmpty()) return; /* separator */
    browser_->scrollToAnchor(topic.anchor);
}

void HelpBrowser::onOpenInBrowser()
{
    QString index = docs_path_ + QStringLiteral("/index.html");
    if (QFile::exists(index))
        QDesktopServices::openUrl(QUrl::fromLocalFile(index));
    else
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://mcaster1.com/encoder.php")));
}

} // namespace mc1
