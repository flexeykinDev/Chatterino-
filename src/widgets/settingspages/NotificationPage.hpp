// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/settingspages/SettingsPage.hpp"

class QComboBox;

namespace chatterino {

class NotificationPage : public SettingsPage
{
    // Without this, tr() here resolves against SettingsPage's context and finds
    // no translations.
    Q_OBJECT

public:
    NotificationPage();

private:
    QComboBox *createToastReactionComboBox();
};

}  // namespace chatterino
