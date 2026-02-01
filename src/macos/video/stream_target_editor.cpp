/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/stream_target_editor.cpp — Stream target add/edit dialog
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "stream_target_editor.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QFileDialog>
#include <QVBoxLayout>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace mc1 {

StreamTargetEditor::StreamTargetEditor(Mode mode, QWidget *parent, Context ctx)
    : QDialog(parent), mode_(mode), context_(ctx)
{
    setWindowTitle(mode == ADD
        ? QStringLiteral("Add Stream Target")
        : QStringLiteral("Edit Stream Target"));
    setMinimumWidth(420);
    buildUI();
}

void StreamTargetEditor::buildUI()
{
    auto *root = new QVBoxLayout(this);

    /* Server settings group */
    auto *grp = new QGroupBox(QStringLiteral("Server Settings"));
    auto *form = new QFormLayout(grp);

    combo_server_type_ = new QComboBox;
    if (context_ == VIDEO) {
        combo_server_type_->addItem(QStringLiteral("Icecast2"));
        combo_server_type_->addItem(QStringLiteral("Mcaster1 DNAS"));
        combo_server_type_->addItem(QStringLiteral("YouTube Live"));
        combo_server_type_->addItem(QStringLiteral("Twitch"));
        combo_server_type_->addItem(QStringLiteral("HLS (Local)"));
    } else {
        combo_server_type_->addItem(QStringLiteral("Icecast2"));
        combo_server_type_->addItem(QStringLiteral("Shoutcast"));
        combo_server_type_->addItem(QStringLiteral("DNAS"));
        combo_server_type_->addItem(QStringLiteral("RTMP"));
    }
    connect(combo_server_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StreamTargetEditor::onServerTypeChanged);
    form->addRow(QStringLiteral("Server Type:"), combo_server_type_);

    lbl_host_ = new QLabel(QStringLiteral("Host:"));
    edit_host_ = new QLineEdit(QStringLiteral("localhost"));
    form->addRow(lbl_host_, edit_host_);

    lbl_port_ = new QLabel(QStringLiteral("Port:"));
    spin_port_ = new QSpinBox;
    spin_port_->setRange(1, 65535);
    spin_port_->setValue(8000);
    form->addRow(lbl_port_, spin_port_);

    lbl_mount_ = new QLabel(QStringLiteral("Mount:"));
    edit_mount_ = new QLineEdit(QStringLiteral("/live"));
    form->addRow(lbl_mount_, edit_mount_);

    lbl_username_ = new QLabel(QStringLiteral("Username:"));
    edit_username_ = new QLineEdit(QStringLiteral("source"));
    form->addRow(lbl_username_, edit_username_);

    lbl_password_ = new QLabel(QStringLiteral("Password:"));
    edit_password_ = new QLineEdit;
    edit_password_->setEchoMode(QLineEdit::Password);
    form->addRow(lbl_password_, edit_password_);

    /* Stream key (YouTube/Twitch RTMP) */
    lbl_stream_key_ = new QLabel(QStringLiteral("Stream Key:"));
    edit_stream_key_ = new QLineEdit;
    edit_stream_key_->setEchoMode(QLineEdit::Password);
    edit_stream_key_->setPlaceholderText(QStringLiteral("xxxx-xxxx-xxxx-xxxx"));
    form->addRow(lbl_stream_key_, edit_stream_key_);

    /* HLS output directory */
    lbl_output_dir_ = new QLabel(QStringLiteral("Output Directory:"));
    auto *dir_row = new QHBoxLayout;
    edit_output_dir_ = new QLineEdit;
    edit_output_dir_->setPlaceholderText(QStringLiteral("/path/to/hls/output"));
    dir_row->addWidget(edit_output_dir_);
    btn_browse_dir_ = new QPushButton(QStringLiteral("Browse..."));
    btn_browse_dir_->setFixedWidth(80);
    connect(btn_browse_dir_, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this,
            QStringLiteral("HLS Output Directory"),
            edit_output_dir_->text());
        if (!dir.isEmpty())
            edit_output_dir_->setText(dir);
    });
    dir_row->addWidget(btn_browse_dir_);
    form->addRow(lbl_output_dir_, dir_row);

    /* HLS segment settings */
    lbl_seg_dur_ = new QLabel(QStringLiteral("Segment Duration:"));
    spin_seg_dur_ = new QSpinBox;
    spin_seg_dur_->setRange(2, 30);
    spin_seg_dur_->setValue(6);
    spin_seg_dur_->setSuffix(QStringLiteral(" sec"));
    form->addRow(lbl_seg_dur_, spin_seg_dur_);

    lbl_max_segs_ = new QLabel(QStringLiteral("Max Segments:"));
    spin_max_segs_ = new QSpinBox;
    spin_max_segs_->setRange(2, 100);
    spin_max_segs_->setValue(5);
    form->addRow(lbl_max_segs_, spin_max_segs_);

    root->addWidget(grp);

    /* Test connection */
    auto *test_row = new QHBoxLayout;
    btn_test_ = new QPushButton(QStringLiteral("Test Connection"));
    btn_test_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #333355; color: #ccccdd; padding: 4px 12px; "
        "border-radius: 3px; }"
        "QPushButton:hover { background: #444466; }"));
    connect(btn_test_, &QPushButton::clicked, this, &StreamTargetEditor::onTestConnection);
    test_row->addWidget(btn_test_);

    lbl_test_result_ = new QLabel;
    lbl_test_result_->setStyleSheet(QStringLiteral("font-size: 11px; padding: 4px;"));
    test_row->addWidget(lbl_test_result_);
    test_row->addStretch();
    root->addLayout(test_row);

    root->addStretch();

    /* OK / Cancel */
    auto *btn_row = new QHBoxLayout;
    btn_row->addStretch();

    btn_cancel_ = new QPushButton(QStringLiteral("Cancel"));
    connect(btn_cancel_, &QPushButton::clicked, this, &QDialog::reject);
    btn_row->addWidget(btn_cancel_);

    btn_ok_ = new QPushButton(mode_ == ADD
        ? QStringLiteral("Add Target")
        : QStringLiteral("Save"));
    btn_ok_->setDefault(true);
    btn_ok_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 6px 16px; "
        "border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #00e8bb; }"));
    connect(btn_ok_, &QPushButton::clicked, this, &QDialog::accept);
    btn_row->addWidget(btn_ok_);

    root->addLayout(btn_row);

    updateFieldVisibility();
}

void StreamTargetEditor::setTarget(const StreamTargetEntry &entry)
{
    /* Server type — handle "DNAS" ↔ "Mcaster1 DNAS" mapping */
    QString type_str = QString::fromStdString(entry.server_type);
    int idx = combo_server_type_->findText(type_str);
    if (idx < 0 && type_str == QStringLiteral("DNAS"))
        idx = combo_server_type_->findText(QStringLiteral("Mcaster1 DNAS"));
    if (idx < 0 && type_str == QStringLiteral("Mcaster1 DNAS"))
        idx = combo_server_type_->findText(QStringLiteral("DNAS"));
    if (idx >= 0) combo_server_type_->setCurrentIndex(idx);

    edit_host_->setText(QString::fromStdString(entry.host));
    spin_port_->setValue(entry.port);
    edit_mount_->setText(QString::fromStdString(entry.mount));
    edit_password_->setText(QString::fromStdString(entry.password));
    edit_stream_key_->setText(QString::fromStdString(entry.stream_key));
    edit_output_dir_->setText(QString::fromStdString(entry.output_dir));
    spin_seg_dur_->setValue(entry.hls_segment_duration);
    spin_max_segs_->setValue(entry.hls_max_segments);
}

StreamTargetEntry StreamTargetEditor::target() const
{
    StreamTargetEntry e;
    e.server_type = combo_server_type_->currentText().toStdString();
    e.host        = edit_host_->text().trimmed().toStdString();
    e.port        = spin_port_->value();
    e.mount       = edit_mount_->text().trimmed().toStdString();
    e.password    = edit_password_->text().toStdString();
    e.stream_key  = edit_stream_key_->text().trimmed().toStdString();
    e.output_dir  = edit_output_dir_->text().trimmed().toStdString();
    e.hls_segment_duration = spin_seg_dur_->value();
    e.hls_max_segments     = spin_max_segs_->value();
    return e;
}

void StreamTargetEditor::onServerTypeChanged(int /*index*/)
{
    updateFieldVisibility();
}

void StreamTargetEditor::updateFieldVisibility()
{
    QString type = combo_server_type_->currentText();
    bool is_hls = (type == QStringLiteral("HLS (Local)"));
    bool is_rtmp = (type == QStringLiteral("YouTube Live") ||
                    type == QStringLiteral("Twitch") ||
                    type == QStringLiteral("RTMP"));

    /* Network fields — hidden for HLS */
    lbl_host_->setVisible(!is_hls);
    edit_host_->setVisible(!is_hls);
    lbl_port_->setVisible(!is_hls);
    spin_port_->setVisible(!is_hls);
    lbl_password_->setVisible(!is_hls);
    edit_password_->setVisible(!is_hls);

    /* Reset mount/username visible first */
    lbl_mount_->setVisible(!is_hls);
    edit_mount_->setVisible(!is_hls);
    lbl_username_->setVisible(!is_hls);
    edit_username_->setVisible(!is_hls);

    /* Stream key — visible for YouTube/Twitch/RTMP */
    lbl_stream_key_->setVisible(is_rtmp);
    edit_stream_key_->setVisible(is_rtmp);

    /* HLS fields — visible only for HLS */
    lbl_output_dir_->setVisible(is_hls);
    edit_output_dir_->setVisible(is_hls);
    btn_browse_dir_->setVisible(is_hls);
    lbl_seg_dur_->setVisible(is_hls);
    spin_seg_dur_->setVisible(is_hls);
    lbl_max_segs_->setVisible(is_hls);
    spin_max_segs_->setVisible(is_hls);

    /* Test connection — hidden for HLS */
    btn_test_->setVisible(!is_hls);

    if (is_hls) {
        /* Nothing extra needed */
    } else if (type == QStringLiteral("YouTube Live")) {
        lbl_mount_->setText(QStringLiteral("App/Path:"));
        edit_mount_->setText(QStringLiteral("live2"));
        edit_mount_->setPlaceholderText(QStringLiteral("live2"));
        lbl_username_->setVisible(false);
        edit_username_->setVisible(false);
        edit_host_->setText(QStringLiteral("a.rtmp.youtube.com"));
        spin_port_->setValue(1935);
    } else if (type == QStringLiteral("Twitch")) {
        lbl_mount_->setText(QStringLiteral("App/Path:"));
        edit_mount_->setText(QStringLiteral("app"));
        edit_mount_->setPlaceholderText(QStringLiteral("app"));
        lbl_username_->setVisible(false);
        edit_username_->setVisible(false);
        edit_host_->setText(QStringLiteral("live.twitch.tv"));
        spin_port_->setValue(1935);
    } else if (type == QStringLiteral("RTMP")) {
        lbl_mount_->setText(QStringLiteral("App/Path:"));
        edit_mount_->setPlaceholderText(QStringLiteral("live"));
        lbl_username_->setVisible(false);
        edit_username_->setVisible(false);
        spin_port_->setValue(1935);
    } else if (type == QStringLiteral("Shoutcast")) {
        lbl_mount_->setVisible(false);
        edit_mount_->setVisible(false);
        lbl_username_->setVisible(false);
        edit_username_->setVisible(false);
        spin_port_->setValue(8000);
    } else if (type == QStringLiteral("DNAS")
            || type == QStringLiteral("Mcaster1 DNAS")) {
        lbl_mount_->setText(QStringLiteral("Mount:"));
        edit_mount_->setPlaceholderText(QStringLiteral("/stream"));
        spin_port_->setValue(9443);
    } else {
        /* Icecast2 */
        lbl_mount_->setText(QStringLiteral("Mount:"));
        edit_mount_->setPlaceholderText(QStringLiteral("/live"));
        spin_port_->setValue(8000);
    }
}

void StreamTargetEditor::onTestConnection()
{
    lbl_test_result_->setText(QStringLiteral("Testing..."));
    lbl_test_result_->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 11px; padding: 4px;"));
    lbl_test_result_->repaint();

    std::string host = edit_host_->text().trimmed().toStdString();
    int port = spin_port_->value();

    if (host.empty()) {
        lbl_test_result_->setText(QStringLiteral("Host is empty"));
        lbl_test_result_->setStyleSheet(QStringLiteral("color: #ff4444; font-size: 11px; padding: 4px;"));
        return;
    }

    /* Non-blocking TCP connect with 3s timeout */
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    int gai = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai != 0) {
        lbl_test_result_->setText(QString("DNS lookup failed: %1")
            .arg(QString::fromUtf8(gai_strerror(gai))));
        lbl_test_result_->setStyleSheet(QStringLiteral("color: #ff4444; font-size: 11px; padding: 4px;"));
        return;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        lbl_test_result_->setText(QStringLiteral("Socket creation failed"));
        lbl_test_result_->setStyleSheet(QStringLiteral("color: #ff4444; font-size: 11px; padding: 4px;"));
        return;
    }

    /* Set non-blocking */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    bool connected = false;
    if (rc == 0) {
        connected = true;
    } else if (errno == EINPROGRESS) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int pr = poll(&pfd, 1, 3000); /* 3s timeout */
        if (pr > 0 && (pfd.revents & POLLOUT)) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            connected = (err == 0);
        }
    }

    ::close(fd);

    if (connected) {
        lbl_test_result_->setText(QString("Connected to %1:%2")
            .arg(QString::fromStdString(host)).arg(port));
        lbl_test_result_->setStyleSheet(QStringLiteral("color: #00d4aa; font-size: 11px; padding: 4px;"));
    } else {
        lbl_test_result_->setText(QString("Connection failed to %1:%2")
            .arg(QString::fromStdString(host)).arg(port));
        lbl_test_result_->setStyleSheet(QStringLiteral("color: #ff4444; font-size: 11px; padding: 4px;"));
    }
}

} // namespace mc1
