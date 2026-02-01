/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * encoder_list_model.cpp — Table model for encoder slot list
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "encoder_list_model.h"
#include <QIcon>

namespace mc1 {

EncoderListModel::EncoderListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

EncoderListModel::EncoderListModel(EncoderConfig::EncoderType filter_type, QObject *parent)
    : QAbstractTableModel(parent), filter_type_(filter_type), has_filter_(true)
{
}

int EncoderListModel::rowCount(const QModelIndex &) const
{
    return static_cast<int>(entries_.size());
}

int EncoderListModel::columnCount(const QModelIndex &) const
{
    return ColCount;
}

QVariant EncoderListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(entries_.size()))
        return {};

    const auto &s = entries_[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case ColSlot:      return s.slot_id;
            case ColName:      return s.name;
            case ColStatus:    return QString::fromUtf8(slot_state_str(s.state));
            case ColCodec:     return s.codec;
            case ColBitrate:   return s.bitrate > 0 ? QString::number(s.bitrate) + " kbps" : QString();
            case ColServer:    return s.server;
            case ColBytesSent: return formatBytes(s.bytes_sent);
            case ColListeners: return s.listeners > 0 ? QString::number(s.listeners) : QStringLiteral("—");
            case ColTrack:     return s.current_title;
        }
    }

    if (role == Qt::ForegroundRole && index.column() == ColStatus) {
        return stateColor(s.state);
    }

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColSlot || index.column() == ColBitrate ||
            index.column() == ColBytesSent || index.column() == ColListeners)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    }

    return {};
}

QVariant EncoderListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
        case ColSlot:      return QStringLiteral("Slot");
        case ColName:      return QStringLiteral("Name");
        case ColStatus:    return QStringLiteral("Status");
        case ColCodec:     return QStringLiteral("Codec");
        case ColBitrate:   return QStringLiteral("Bitrate");
        case ColServer:    return QStringLiteral("Server");
        case ColBytesSent: return QStringLiteral("Sent");
        case ColListeners: return QStringLiteral("Listeners");
        case ColTrack:     return QStringLiteral("Current Track");
    }
    return {};
}

void EncoderListModel::setSlots(const std::vector<SlotDisplayInfo> &data)
{
    beginResetModel();
    entries_ = data;
    endResetModel();
}

void EncoderListModel::updateSlot(int slot_id, const SlotDisplayInfo &info)
{
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].slot_id == slot_id) {
            entries_[i] = info;
            emit dataChanged(index(static_cast<int>(i), 0),
                             index(static_cast<int>(i), ColCount - 1));
            return;
        }
    }
}

void EncoderListModel::addPlaceholderSlots(int count)
{
    beginResetModel();
    entries_.clear();
    configs_.clear();
    for (int i = 1; i <= count; ++i) {
        SlotDisplayInfo s;
        s.slot_id = i;
        s.name    = QString("Encoder %1").arg(i);
        s.state   = SlotState::IDLE;
        s.codec   = QStringLiteral("MP3");
        s.bitrate = 128;
        s.server  = QStringLiteral("—");
        entries_.push_back(s);

        EncoderConfig cfg;
        cfg.slot_id      = i;
        cfg.name         = s.name.toStdString();
        cfg.bitrate_kbps = 128;
        configs_.push_back(cfg);
    }
    endResetModel();
}

const SlotDisplayInfo *EncoderListModel::slotAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return nullptr;
    return &entries_[static_cast<size_t>(row)];
}

/* Phase M2: config-backed operations */

SlotDisplayInfo EncoderListModel::displayFromConfig(const EncoderConfig &cfg)
{
    SlotDisplayInfo s;
    s.slot_id = cfg.slot_id;
    s.name    = QString::fromStdString(cfg.name);
    s.state   = SlotState::IDLE;
    s.codec   = QString::fromUtf8(codec_str(cfg.codec));
    s.bitrate = cfg.bitrate_kbps;
    s.server  = QString::fromStdString(cfg.stream_target.host) +
                QStringLiteral(":") + QString::number(cfg.stream_target.port);
    return s;
}

void EncoderListModel::addSlot(const EncoderConfig &cfg)
{
    /* Phase M7: skip if this model filters by type and cfg doesn't match */
    if (has_filter_ && cfg.encoder_type != filter_type_)
        return;

    int row = static_cast<int>(entries_.size());
    beginInsertRows({}, row, row);
    entries_.push_back(displayFromConfig(cfg));
    configs_.push_back(cfg);
    endInsertRows();
}

void EncoderListModel::updateSlot(int row, const EncoderConfig &cfg)
{
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return;
    entries_[static_cast<size_t>(row)] = displayFromConfig(cfg);
    /* Grow configs_ if needed — never silently drop config updates */
    if (row >= static_cast<int>(configs_.size()))
        configs_.resize(static_cast<size_t>(row) + 1);
    configs_[static_cast<size_t>(row)] = cfg;
    emit dataChanged(index(row, 0), index(row, ColCount - 1));
}

void EncoderListModel::removeSlot(int row)
{
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return;
    beginRemoveRows({}, row, row);
    entries_.erase(entries_.begin() + row);
    if (row < static_cast<int>(configs_.size()))
        configs_.erase(configs_.begin() + row);
    endRemoveRows();
}

EncoderConfig EncoderListModel::configAt(int row) const
{
    if (row >= 0 && row < static_cast<int>(configs_.size()))
        return configs_[static_cast<size_t>(row)];
    /* Fallback: build a minimal config from display info */
    EncoderConfig cfg;
    if (row >= 0 && row < static_cast<int>(entries_.size())) {
        const auto &s = entries_[static_cast<size_t>(row)];
        cfg.slot_id       = s.slot_id;
        cfg.name          = s.name.toStdString();
        cfg.bitrate_kbps  = s.bitrate;
    }
    return cfg;
}

bool EncoderListModel::hasConfig(int row) const
{
    return row >= 0 && row < static_cast<int>(configs_.size());
}

int EncoderListModel::configCount() const
{
    return static_cast<int>(configs_.size());
}

void EncoderListModel::clearAll()
{
    beginResetModel();
    entries_.clear();
    configs_.clear();
    endResetModel();
}

QColor EncoderListModel::stateColor(SlotState s)
{
    switch (s) {
        case SlotState::IDLE:         return QColor(140, 140, 160);
        case SlotState::STARTING:     return QColor(100, 180, 255);
        case SlotState::CONNECTING:   return QColor(100, 180, 255);
        case SlotState::LIVE:         return QColor(0, 220, 80);
        case SlotState::RECONNECTING: return QColor(255, 180, 0);
        case SlotState::SLEEP:        return QColor(255, 140, 0);
        case SlotState::ERROR:        return QColor(255, 60, 60);
        case SlotState::STOPPING:     return QColor(180, 180, 200);
    }
    return QColor(140, 140, 160);
}

QString EncoderListModel::formatBytes(uint64_t bytes)
{
    if (bytes == 0) return QStringLiteral("—");
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024ULL * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}

/* Phase M3: Map EncoderSlot::State → SlotState for display */
static SlotState mapState(EncoderSlot::State s)
{
    switch (s) {
        case EncoderSlot::State::IDLE:         return SlotState::IDLE;
        case EncoderSlot::State::STARTING:     return SlotState::STARTING;
        case EncoderSlot::State::CONNECTING:   return SlotState::CONNECTING;
        case EncoderSlot::State::LIVE:         return SlotState::LIVE;
        case EncoderSlot::State::RECONNECTING: return SlotState::RECONNECTING;
        case EncoderSlot::State::SLEEP:        return SlotState::SLEEP;
        case EncoderSlot::State::ERROR:        return SlotState::ERROR;
        case EncoderSlot::State::STOPPING:     return SlotState::STOPPING;
    }
    return SlotState::IDLE;
}

void EncoderListModel::updateLiveStats(const std::vector<EncoderSlot::Stats> &stats)
{
    for (const auto &st : stats) {
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].slot_id == st.slot_id) {
                entries_[i].state         = mapState(st.state);
                entries_[i].bytes_sent    = st.bytes_sent;
                entries_[i].listeners     = st.listeners;
                entries_[i].current_title = QString::fromStdString(st.current_title);
                emit dataChanged(index(static_cast<int>(i), ColStatus),
                                 index(static_cast<int>(i), ColTrack));
                break;
            }
        }
    }
}

} // namespace mc1
