/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * encoder_type_chooser.h — Dialog for choosing encoder type (Radio/Podcast/TV-Video)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_ENCODER_TYPE_CHOOSER_H
#define MC1_ENCODER_TYPE_CHOOSER_H

#include <QDialog>
#include "config_types.h"

namespace mc1 {

class EncoderTypeChooser : public QDialog {
    Q_OBJECT
public:
    explicit EncoderTypeChooser(QWidget *parent = nullptr);

    EncoderConfig::EncoderType selectedType() const { return selected_type_; }

private:
    EncoderConfig::EncoderType selected_type_ = EncoderConfig::EncoderType::RADIO;
};

} // namespace mc1

#endif // MC1_ENCODER_TYPE_CHOOSER_H
