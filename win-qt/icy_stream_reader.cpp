/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * icy_stream_reader.cpp — ICY1/ICY2.2 stream reader implementation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "icy_stream_reader.h"

#include <QTcpSocket>
#include <QDateTime>
#include <QUrl>
#include <QRegularExpression>

#include <algorithm>

namespace mc1 {

IcyStreamReader::IcyStreamReader(QObject *parent)
    : QObject(parent)
    , sock_(new QTcpSocket(this))
{
    connect(sock_, &QTcpSocket::connected,    this, &IcyStreamReader::onConnected);
    connect(sock_, &QTcpSocket::readyRead,    this, &IcyStreamReader::onReadyRead);
    connect(sock_, &QTcpSocket::disconnected, this, &IcyStreamReader::onDisconnected);
    connect(sock_, &QAbstractSocket::errorOccurred,
            this, &IcyStreamReader::onSocketError);
}

IcyStreamReader::~IcyStreamReader()
{
    disconnectFromStream();
}

void IcyStreamReader::connectToUrl(const QString &url_str)
{
    disconnectFromStream();
    state_       = State::ReadingResponse;
    buf_.clear();
    audio_bytes_ = 0;
    expect_meta_ = false;
    meta_len_    = 0;
    headers_     = IcyHeaders{};

    QUrl url(url_str);
    QString host  = url.host();
    quint16 port  = static_cast<quint16>(url.port(80));
    QString path  = url.path();
    if (path.isEmpty()) path = QStringLiteral("/");

    emit protocolEvent(QString(QStringLiteral("[%1] Connecting to %2:%3%4"))
                       .arg(ts()).arg(host).arg(port).arg(path));

    sock_->connectToHost(host, port);

    /* Store path/host/port for the request we'll send onConnected */
    sock_->setProperty("_path", path);
    sock_->setProperty("_host", host);
    sock_->setProperty("_port", port);
}

void IcyStreamReader::disconnectFromStream()
{
    if (sock_->state() != QAbstractSocket::UnconnectedState) {
        sock_->abort();
        buf_.clear();
        state_ = State::Idle;
        emit protocolEvent(QString(QStringLiteral("[%1] Disconnected")).arg(ts()));
        emit disconnected();
    }
}

bool IcyStreamReader::isConnected() const
{
    return sock_->state() == QAbstractSocket::ConnectedState &&
           state_ == State::ReadingAudio;
}

/* ── Private slots ──────────────────────────────────────────────────────── */

void IcyStreamReader::onConnected()
{
    QString path = sock_->property("_path").toString();
    QString host = sock_->property("_host").toString();
    int     port = sock_->property("_port").toInt();

    QString host_hdr = (port == 80)
        ? host
        : QString(QStringLiteral("%1:%2")).arg(host).arg(port);

    /* Build HTTP/1.0-style request with ICY metadata negotiation.
       ICY2.2 servers also accept Icy-MetaData: 1 — they upgrade the
       response with icy2-* headers automatically when supported. */
    QByteArray req;
    req += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
    req += "Host: " + host_hdr.toUtf8() + "\r\n";
    req += "User-Agent: Mcaster1-PreviewAudioStudio/1.2.0\r\n";
    req += "Accept: */*\r\n";
    req += "Icy-MetaData: 1\r\n";
    req += "Connection: close\r\n";
    req += "\r\n";

    sock_->write(req);

    emit protocolEvent(QString(QStringLiteral("[%1] TCP connected — sent HTTP/1.1 request")).arg(ts()));
    emit protocolEvent(QString(QStringLiteral("[%1] \u2192 GET %2 HTTP/1.1")).arg(ts()).arg(path));
    emit protocolEvent(QString(QStringLiteral("[%1] \u2192 Host: %2")).arg(ts()).arg(host_hdr));
    emit protocolEvent(QString(QStringLiteral("[%1] \u2192 Icy-MetaData: 1  (ICY1/ICY2.2 negotiation)")).arg(ts()));
}

void IcyStreamReader::onReadyRead()
{
    buf_ += sock_->readAll();

    if (state_ == State::ReadingResponse || state_ == State::ReadingHeaders) {
        /* Detect end of HTTP/ICY header block (\r\n\r\n or \n\n) */
        int hdr_end = buf_.indexOf("\r\n\r\n");
        int hdr_sep = 4;
        if (hdr_end < 0) { hdr_end = buf_.indexOf("\n\n"); hdr_sep = 2; }

        if (hdr_end >= 0) {
            QByteArray hdr_bytes = buf_.left(hdr_end);
            buf_ = buf_.mid(hdr_end + hdr_sep);
            parseHeaderBlock(hdr_bytes);
            state_ = State::ReadingAudio;
            emit connected(headers_);
        }
    }

    if (state_ == State::ReadingAudio)
        processAudio();
}

void IcyStreamReader::onSocketError()
{
    emit streamError(sock_->errorString());
    emit protocolEvent(QString(QStringLiteral("[%1] \u2715 SOCKET ERROR: %2"))
                       .arg(ts()).arg(sock_->errorString()));
}

void IcyStreamReader::onDisconnected()
{
    if (state_ != State::Idle) {
        state_ = State::Idle;
        emit disconnected();
        emit protocolEvent(QString(QStringLiteral("[%1] Stream connection closed")).arg(ts()));
    }
}

/* ── Header parsing (ICY1 + ICY2.2) ────────────────────────────────────── */

void IcyStreamReader::parseHeaderBlock(const QByteArray &hdr_bytes)
{
    const QList<QByteArray> lines = hdr_bytes.split('\n');
    bool first = true;

    for (const QByteArray &raw_line : lines) {
        QByteArray line = raw_line.trimmed();
        if (line.isEmpty()) continue;

        if (first) {
            first = false;
            /* Status/version line: "ICY 200 OK" or "HTTP/1.1 200 OK" */
            int sp = line.indexOf(' ');
            headers_.icy_version = QString::fromUtf8(sp > 0 ? line.left(sp) : line);
            state_ = State::ReadingHeaders;
            emit protocolEvent(QString(QStringLiteral("[%1] \u2190 %2"))
                               .arg(ts()).arg(QString::fromUtf8(line)));
            continue;
        }

        int colon = line.indexOf(':');
        if (colon < 0) continue;

        const QString key = QString::fromUtf8(line.left(colon)).trimmed().toLower();
        const QString val = QString::fromUtf8(line.mid(colon + 1)).trimmed();
        headers_.all[key] = val;

        emit protocolEvent(QString(QStringLiteral("[%1] \u2190 %2: %3"))
                           .arg(ts()).arg(key).arg(val));

        /* ── ICY1 standard fields ─────────────────────────────────────── */
        if      (key == QStringLiteral("icy-name"))        headers_.icy_name        = val;
        else if (key == QStringLiteral("icy-genre"))       headers_.icy_genre       = val;
        else if (key == QStringLiteral("icy-url"))         headers_.icy_url         = val;
        else if (key == QStringLiteral("icy-description")) headers_.icy_description = val;
        else if (key == QStringLiteral("content-type"))    headers_.content_type    = val;
        else if (key == QStringLiteral("server"))          headers_.server          = val;
        else if (key == QStringLiteral("icy-audio-info"))  headers_.icy_audio_info  = val;
        else if (key == QStringLiteral("icy-pub"))         headers_.icy_pub         = val.toInt();
        else if (key == QStringLiteral("icy-br") ||
                 key == QStringLiteral("icy-bitrate"))
            headers_.bitrate_kbps = val.toInt();
        else if (key == QStringLiteral("icy-metaint"))
            headers_.metaint = val.toInt();

        /* ── ICY2.2 extended fields ───────────────────────────────────── */
        else if (key.startsWith(QStringLiteral("icy2-"))) {
            headers_.is_icy2 = true;
            if      (key == QStringLiteral("icy2-twitter"))   headers_.icy2_twitter   = val;
            else if (key == QStringLiteral("icy2-facebook"))  headers_.icy2_facebook  = val;
            else if (key == QStringLiteral("icy2-instagram")) headers_.icy2_instagram = val;
            else if (key == QStringLiteral("icy2-email"))     headers_.icy2_email     = val;
            else if (key == QStringLiteral("icy2-language"))  headers_.icy2_language  = val;
            else if (key == QStringLiteral("icy2-logo"))      headers_.icy2_logo      = val;
            else if (key == QStringLiteral("icy2-country"))   headers_.icy2_country   = val;
            else if (key == QStringLiteral("icy2-timezone"))  headers_.icy2_timezone  = val;
            else if (key == QStringLiteral("icy2-irc"))       headers_.icy2_irc       = val;
            else if (key == QStringLiteral("icy2-aim"))       headers_.icy2_aim       = val;
            else if (key == QStringLiteral("icy2-icq"))       headers_.icy2_icq       = val;
        }
    }

    emit protocolEvent(
        QString(QStringLiteral("[%1] Headers complete \u2014 "
                               "metaint=%2  content-type=%3  ICY2.2=%4  "
                               "bitrate=%5 kbps"))
        .arg(ts())
        .arg(headers_.metaint)
        .arg(headers_.content_type)
        .arg(headers_.is_icy2 ? QStringLiteral("YES") : QStringLiteral("no"))
        .arg(headers_.bitrate_kbps));

    if (headers_.is_icy2) {
        emit protocolEvent(
            QString(QStringLiteral("[%1] ICY2.2 \u2014 lang=%2  country=%3  "
                                   "twitter=%4  email=%5"))
            .arg(ts())
            .arg(headers_.icy2_language.isEmpty()  ? QStringLiteral("-") : headers_.icy2_language)
            .arg(headers_.icy2_country.isEmpty()   ? QStringLiteral("-") : headers_.icy2_country)
            .arg(headers_.icy2_twitter.isEmpty()   ? QStringLiteral("-") : headers_.icy2_twitter)
            .arg(headers_.icy2_email.isEmpty()     ? QStringLiteral("-") : headers_.icy2_email));
    }
}

/* ── Audio data + ICY metadata block interleaving ───────────────────────── */

void IcyStreamReader::processAudio()
{
    if (headers_.metaint <= 0) {
        /* No metadata interleaving (or not yet known) — discard raw bytes */
        buf_.clear();
        return;
    }

    while (!buf_.isEmpty()) {
        if (!expect_meta_) {
            /* Consume audio bytes up to next metaint boundary */
            int remaining_audio = headers_.metaint - audio_bytes_;
            int available       = buf_.size();
            int to_consume      = std::min(available, remaining_audio);

            /* Discard audio bytes (QMediaPlayer handles actual playback) */
            buf_.remove(0, to_consume);
            audio_bytes_ += to_consume;

            if (audio_bytes_ >= headers_.metaint) {
                audio_bytes_ = 0;
                expect_meta_ = true;
                meta_len_    = -1;   /* -1 = need to read the 1-byte length prefix */
            } else {
                break; /* need more data */
            }
        }

        if (expect_meta_) {
            if (meta_len_ < 0) {
                /* Read 1-byte metadata length (actual bytes = raw * 16) */
                if (buf_.isEmpty()) break;
                int raw_len = static_cast<uint8_t>(buf_.at(0));
                buf_.remove(0, 1);
                meta_len_ = raw_len * 16;
                if (meta_len_ == 0) {
                    expect_meta_ = false;  /* empty metadata block — common filler */
                    continue;
                }
            }
            /* Wait until we have the full metadata block */
            if (buf_.size() < meta_len_) break;
            QByteArray meta = buf_.left(meta_len_);
            buf_.remove(0, meta_len_);
            expect_meta_ = false;
            parseMetaBlock(meta);
        }
    }
}

/* ── ICY metadata block parser ──────────────────────────────────────────── */

void IcyStreamReader::parseMetaBlock(const QByteArray &raw)
{
    /* Trim null padding at the end of the fixed-size block */
    int end = raw.indexOf('\0');
    QString meta_str = QString::fromUtf8(end >= 0 ? raw.left(end) : raw).trimmed();
    if (meta_str.isEmpty()) return;

    emit protocolEvent(QString(QStringLiteral("[%1] ICY Meta: %2")).arg(ts()).arg(meta_str));

    /* Extract all Key='Value'; pairs per ICY1/ICY2.2 metadata spec */
    QString title, stream_url;
    static const QRegularExpression re(QStringLiteral(R"((\w[\w-]*)='([^']*)')"));
    QRegularExpressionMatchIterator it = re.globalMatch(meta_str);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        const QString k = m.captured(1).toLower();
        const QString v = m.captured(2).trimmed();
        if (k == QStringLiteral("streamtitle"))
            title = v;
        else if (k == QStringLiteral("streamurl"))
            stream_url = v;
    }

    emit metaUpdated(title, stream_url, meta_str);
}

/* ── Helpers ────────────────────────────────────────────────────────────── */

QString IcyStreamReader::ts() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}

} // namespace mc1
