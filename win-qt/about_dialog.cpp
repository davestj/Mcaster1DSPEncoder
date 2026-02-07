/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * about_dialog.cpp — About dialog
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "about_dialog.h"
#include "app.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace mc1 {

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("About Mcaster1 DSP Encoder"));
    setFixedSize(380, 280);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(10);
    lay->setContentsMargins(24, 20, 24, 16);

    auto *title = new QLabel(QStringLiteral(
        "<h2 style='margin:0'>Mcaster1 DSP Encoder</h2>"));
    title->setAlignment(Qt::AlignCenter);
    lay->addWidget(title);

    auto *ver = new QLabel(
        QStringLiteral("<b>Version</b> ") + App::versionString() +
        QStringLiteral("  (") + App::buildPhase() + QStringLiteral(")"));
    ver->setAlignment(Qt::AlignCenter);
    lay->addWidget(ver);

    auto *author = new QLabel(QStringLiteral(
        "<p>by <b>David St. John</b><br>"
        "<a href='mailto:davestj@gmail.com'>davestj@gmail.com</a></p>"
        "<p>Based on Edcast v3 by Ed Zaleski</p>"));
    author->setAlignment(Qt::AlignCenter);
    author->setOpenExternalLinks(true);
    lay->addWidget(author);

    auto *links = new QLabel(QStringLiteral(
        "<p><a href='https://github.com/davestj/Mcaster1DSPEncoder'>GitHub Repository</a>"
        " &middot; <a href='https://mcaster1.com/encoder.php'>mcaster1.com</a></p>"));
    links->setAlignment(Qt::AlignCenter);
    links->setOpenExternalLinks(true);
    lay->addWidget(links);

    auto *platform = new QLabel(QStringLiteral(
        "<p><small>"
#ifdef _WIN32
        "Windows Qt 6 Build — x64<br>"
        "Audio: PortAudio + WASAPI<br>"
#else
        "macOS Qt 6 Build — ARM64 (Apple Silicon)<br>"
        "Audio: PortAudio + ScreenCaptureKit<br>"
#endif
        "Codecs: LAME, Vorbis, Opus, FLAC, fdk-aac</small></p>"));
    platform->setAlignment(Qt::AlignCenter);
    lay->addWidget(platform);

    lay->addStretch();

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    lay->addWidget(bb);
}

} // namespace mc1
