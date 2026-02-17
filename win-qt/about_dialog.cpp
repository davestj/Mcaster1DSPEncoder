/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * about_dialog.cpp — About dialog with ecosystem links
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
    setFixedSize(440, 400);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(8);
    lay->setContentsMargins(24, 20, 24, 16);

    auto *title = new QLabel(QStringLiteral(
        "<h2 style='margin:0'>Mcaster1 DSP Encoder</h2>"
        "<p style='margin:2px 0 0 0; color:#888;'>Next Generation Broadcast DSP Encoder</p>"));
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
        "<p style='font-size:11px;'>Based on Edcast v3 by Ed Zaleski</p>"));
    author->setAlignment(Qt::AlignCenter);
    author->setOpenExternalLinks(true);
    lay->addWidget(author);

    auto *links = new QLabel(QStringLiteral(
        "<p>"
        "<a href='https://mcaster1.com/encoder.php'>Encoder Product Page</a>"
        " &middot; <a href='https://github.com/davestj/Mcaster1DSPEncoder'>GitHub</a>"
        "</p>"
        "<p style='font-size:11px;'>"
        "<a href='https://mcaster1.com/mcaster1_dnas.php'>Mcaster1 DNAS</a>"
        " &middot; <a href='https://mcaster1.com/mcaster1amp.php'>Mcaster1AMP Player</a>"
        " &middot; <a href='https://mcaster1.com/mcaster1studio.php'>Mcaster1 Studio</a>"
        "<br>"
        "<a href='https://mcaster1.com/audiopipes.php'>AudioPipe</a>"
        " &middot; <a href='https://mcaster1.com/tagstack.php'>TagStack</a>"
        " &middot; <a href='https://mcaster1.com/mcaster1_castit.php'>CastIt</a>"
        "</p>"));
    links->setAlignment(Qt::AlignCenter);
    links->setOpenExternalLinks(true);
    lay->addWidget(links);

#ifdef _WIN32
    static const QString platform_html =
        "<p><small>"
        "Windows Qt 6 Build &mdash; x64<br>"
        "Audio: PortAudio (WASAPI) + WASAPI Loopback<br>"
        "Video: MF H.264 (NVENC/QSV/AMF), VP8/VP9 (libvpx)<br>"
        "Codecs: LAME, Vorbis, Opus, FLAC, fdk-aac<br>"
        "12 broadcast transitions &middot; Virtual Camera</small></p>";
#else
    static const QString platform_html =
        "<p><small>"
        "macOS Qt 6 Build &mdash; ARM64 (Apple Silicon)<br>"
        "Audio: PortAudio + ScreenCaptureKit<br>"
        "Video: AVFoundation, VP8/VP9 (libvpx)<br>"
        "Codecs: LAME, Vorbis, Opus, FLAC, fdk-aac<br>"
        "12 broadcast transitions &middot; Virtual Camera</small></p>";
#endif
    auto *platform = new QLabel(platform_html);
    platform->setAlignment(Qt::AlignCenter);
    lay->addWidget(platform);

    lay->addStretch();

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    lay->addWidget(bb);
}

} // namespace mc1
