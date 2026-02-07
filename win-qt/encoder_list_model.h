/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * encoder_list_model.h — Table model for encoder slot list
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_ENCODER_LIST_MODEL_H
#define MC1_ENCODER_LIST_MODEL_H

#include <QAbstractTableModel>
#include <QColor>
#include <vector>
#include "config_types.h"
#include "encoder_slot.h"

namespace mc1 {

struct SlotDisplayInfo {
    int         slot_id    = 0;
    QString     name;
    SlotState   state      = SlotState::IDLE;
    QString     codec;
    int         bitrate    = 0;
    QString     server;
    uint64_t    bytes_sent = 0;
    int         listeners  = 0;
    QString     current_title;
};

class EncoderListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColSlot = 0, ColName, ColStatus, ColCodec,
        ColBitrate, ColServer, ColBytesSent, ColListeners, ColTrack,
        ColCount
    };

    explicit EncoderListModel(QObject *parent = nullptr);
    explicit EncoderListModel(EncoderConfig::EncoderType filter_type, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setSlots(const std::vector<SlotDisplayInfo> &data);
    void updateSlot(int slot_id, const SlotDisplayInfo &info);
    void addPlaceholderSlots(int count);

    const SlotDisplayInfo *slotAt(int row) const;

    /* Phase M2: config-backed operations */
    void addSlot(const EncoderConfig &cfg);
    void updateSlot(int row, const EncoderConfig &cfg);
    void removeSlot(int row);
    EncoderConfig configAt(int row) const;
    bool hasConfig(int row) const;
    int  configCount() const;
    void clearAll();

    /* Phase M3: update display from live pipeline stats */
    void updateLiveStats(const std::vector<EncoderSlot::Stats> &stats);

    /* Phase M4: public utility for status bar bandwidth display */
    static QString formatBytes(uint64_t bytes);

private:
    static QColor stateColor(SlotState s);
    static SlotDisplayInfo displayFromConfig(const EncoderConfig &cfg);

    std::vector<SlotDisplayInfo> entries_;
    std::vector<EncoderConfig>   configs_;

    /* Phase M7: type filter (RADIO/PODCAST/TV_VIDEO) — default RADIO */
    EncoderConfig::EncoderType filter_type_ = EncoderConfig::EncoderType::RADIO;
    bool has_filter_ = false;
};

} // namespace mc1

#endif // MC1_ENCODER_LIST_MODEL_H
