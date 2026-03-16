/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * icy_stream_reader.h — ICY1/ICY2.2 HTTP audio stream reader
 *
 * Connects to an Icecast/Shoutcast/DNAS stream over TCP, sends
 * "Icy-MetaData: 1" in the request, parses all ICY1 and ICY2.2
 * response headers, tracks audio bytes to detect metadata blocks
 * (icy-metaint), and emits signals for every metadata update and
 * protocol event.
 *
 * ICY2.2 extended headers supported:
 *   icy2-twitter, icy2-facebook, icy2-instagram, icy2-email,
 *   icy2-language, icy2-logo, icy2-country, icy2-timezone,
 *   icy2-genre, icy2-pub, icy2-irc, icy2-aim, icy2-icq
 *
 * Does NOT decode audio — audio playback is handled by QMediaPlayer.
 * This class only monitors the raw protocol for metadata and diagnostics.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QByteArray>

class QTcpSocket;

namespace mc1 {

class IcyStreamReader : public QObject {
    Q_OBJECT

public:
    struct IcyHeaders {
        /* ── ICY1 standard fields ─────────────────────────────────────── */
        QString icy_version;            /* "ICY" or "HTTP/1.1" */
        QString icy_name;               /* icy-name */
        QString icy_genre;              /* icy-genre */
        QString icy_url;                /* icy-url */
        QString icy_description;        /* icy-description */
        QString content_type;           /* content-type: audio/mpeg etc. */
        QString server;                 /* Server header (Icecast version etc.) */
        QString icy_audio_info;         /* icy-audio-info: bitrate=128;samplerate=44100;channels=2 */
        int     bitrate_kbps  = 0;      /* icy-br or icy-bitrate */
        int     metaint       = 0;      /* icy-metaint: metadata interval in audio bytes */
        int     icy_pub       = -1;     /* icy-pub: 0/1 */

        /* ── ICY2.2 extended fields ───────────────────────────────────── */
        bool    is_icy2       = false;  /* true if any icy2-* headers present */
        QString icy2_twitter;           /* icy2-twitter: @handle */
        QString icy2_facebook;          /* icy2-facebook: URL or page name */
        QString icy2_instagram;         /* icy2-instagram: @handle */
        QString icy2_email;             /* icy2-email: contact address */
        QString icy2_language;          /* icy2-language: en, es, fr, etc. */
        QString icy2_logo;              /* icy2-logo: URL to station logo */
        QString icy2_country;           /* icy2-country: ISO 3166-1 alpha-2 */
        QString icy2_timezone;          /* icy2-timezone: IANA timezone */
        QString icy2_irc;               /* icy2-irc: IRC channel */
        QString icy2_aim;               /* icy2-aim: AIM handle */
        QString icy2_icq;               /* icy2-icq: ICQ number */

        /* ── All headers verbatim ─────────────────────────────────────── */
        QMap<QString, QString> all;
    };

    explicit IcyStreamReader(QObject *parent = nullptr);
    ~IcyStreamReader() override;

    void connectToUrl(const QString &url);
    void disconnectFromStream();

    const IcyHeaders &icyHeaders() const { return headers_; }
    bool isConnected() const;

signals:
    /* Emitted once ICY/HTTP headers are fully parsed */
    void connected(const mc1::IcyStreamReader::IcyHeaders &hdrs);

    /* Emitted on every ICY metadata block update */
    void metaUpdated(const QString &stream_title, const QString &stream_url,
                     const QString &raw_meta);

    /* Protocol/debug events — timestamped human-readable strings */
    void protocolEvent(const QString &line);

    void disconnected();
    void streamError(const QString &msg);

private slots:
    void onConnected();
    void onReadyRead();
    void onSocketError();
    void onDisconnected();

private:
    enum class State { Idle, ReadingResponse, ReadingHeaders, ReadingAudio };

    void parseHeaderBlock(const QByteArray &hdr_bytes);
    void processAudio();
    void parseMetaBlock(const QByteArray &raw);
    QString ts() const;     /* current timestamp string [HH:MM:SS] */

    QTcpSocket  *sock_   = nullptr;
    State        state_  = State::Idle;
    IcyHeaders   headers_;

    QByteArray   buf_;          /* raw receive buffer */
    int          audio_bytes_   = 0;  /* bytes received in current metaint interval */
    bool         expect_meta_   = false;
    int          meta_len_      = 0;  /* pending meta block byte length (-1 = need length byte) */
};

} // namespace mc1
