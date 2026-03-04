/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * encoder_type_chooser.cpp — Dialog for choosing encoder type
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "encoder_type_chooser.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace mc1 {

/* ── TypeCardButton — custom button with hover/pressed icon states ── */
class TypeCardButton : public QAbstractButton {
public:
    TypeCardButton(const QString &normal_icon, const QString &hover_icon,
                   QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , pix_normal_(QIcon(normal_icon).pixmap(56, 56))
        , pix_hover_(QIcon(hover_icon).pixmap(56, 56))
    {
        setFixedSize(140, 100);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }

protected:
    void enterEvent(QEnterEvent *) override {
        hovered_ = true;
        /* Add glow effect on hover */
        auto *glow = new QGraphicsDropShadowEffect(this);
        glow->setColor(QColor(0, 212, 170, 120));
        glow->setBlurRadius(16);
        glow->setOffset(0, 0);
        setGraphicsEffect(glow);
        update();
    }

    void leaveEvent(QEvent *) override {
        hovered_ = false;
        pressed_ = false;
        setGraphicsEffect(nullptr);
        update();
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            pressed_ = true;
            update();
        }
        QAbstractButton::mousePressEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && pressed_) {
            pressed_ = false;
            update();
            if (rect().contains(e->pos()))
                emit clicked();
        }
        QAbstractButton::mouseReleaseEvent(e);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        /* Background + border */
        QColor bg = pressed_  ? QColor(0x0a, 0x0a, 0x1e)
                   : hovered_ ? QColor(0x22, 0x22, 0x44)
                              : QColor(0x1a, 0x1a, 0x2e);
        QColor border = pressed_  ? QColor(0x00, 0xb0, 0x88)
                       : hovered_ ? QColor(0x00, 0xd4, 0xaa)
                                  : QColor(0x33, 0x33, 0x55);
        int bw = hovered_ ? 2 : 2;

        QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        if (pressed_) r.adjust(0, 2, 0, 2);  /* 2px downward shift */

        p.setPen(QPen(border, bw));
        p.setBrush(bg);
        p.drawRoundedRect(r, 10, 10);

        /* Icon centered */
        const QPixmap &pix = hovered_ ? pix_hover_ : pix_normal_;
        int ix = (int)(r.x() + (r.width() - pix.width() / pix.devicePixelRatio()) / 2);
        int iy = (int)(r.y() + (r.height() - pix.height() / pix.devicePixelRatio()) / 2);
        p.drawPixmap(ix, iy, pix);
    }

    QSize sizeHint() const override { return QSize(140, 100); }

private:
    QPixmap pix_normal_;
    QPixmap pix_hover_;
    bool hovered_ = false;
    bool pressed_ = false;
};

/* ── EncoderTypeChooser dialog ── */

EncoderTypeChooser::EncoderTypeChooser(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Choose Encoder Type"));
    setFixedSize(560, 280);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("What type of encoder do you want to create?"));
    QFont tf = title->font();
    tf.setPointSize(14);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    root->addWidget(title);

    root->addSpacing(10);

    auto *cards = new QHBoxLayout;
    cards->setSpacing(20);
    cards->addStretch();

    /* Helper: create a card with label + description */
    auto makeCard = [this, cards](const QString &normal, const QString &hover,
                                   const QString &label, const QString &desc,
                                   EncoderConfig::EncoderType type) {
        auto *col = new QVBoxLayout;
        col->setAlignment(Qt::AlignCenter);
        col->setSpacing(6);

        auto *btn = new TypeCardButton(normal, hover, this);
        connect(btn, &QAbstractButton::clicked, this, [this, type]() {
            selected_type_ = type;
            accept();
        });
        col->addWidget(btn, 0, Qt::AlignCenter);

        auto *lbl = new QLabel(label);
        lbl->setAlignment(Qt::AlignCenter);
        QFont f = lbl->font();
        f.setBold(true);
        f.setPointSize(12);
        lbl->setFont(f);
        col->addWidget(lbl);

        auto *d = new QLabel(desc);
        d->setAlignment(Qt::AlignCenter);
        d->setStyleSheet(QStringLiteral("color: #667788; font-size: 10px;"));
        col->addWidget(d);

        cards->addLayout(col);
    };

    makeCard(QStringLiteral(":/icons/radio-card.svg"),
             QStringLiteral(":/icons/radio-card-hover.svg"),
             QStringLiteral("Radio"),
             QStringLiteral("Live audio streaming"),
             EncoderConfig::EncoderType::RADIO);

    makeCard(QStringLiteral(":/icons/podcast-card.svg"),
             QStringLiteral(":/icons/podcast-card-hover.svg"),
             QStringLiteral("Podcast"),
             QStringLiteral("Record & publish episodes"),
             EncoderConfig::EncoderType::PODCAST);

    makeCard(QStringLiteral(":/icons/tv-video-card.svg"),
             QStringLiteral(":/icons/tv-video-card-hover.svg"),
             QStringLiteral("TV/Video"),
             QStringLiteral("Live video streaming"),
             EncoderConfig::EncoderType::TV_VIDEO);

    cards->addStretch();
    root->addLayout(cards);
    root->addStretch();

    auto *cancel = new QPushButton(QStringLiteral("Cancel"));
    cancel->setFixedWidth(80);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto *btn_row = new QHBoxLayout;
    btn_row->addStretch();
    btn_row->addWidget(cancel);
    root->addLayout(btn_row);
}

} // namespace mc1
