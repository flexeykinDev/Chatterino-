// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BasePopup.hpp"

#include <QString>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace chatterino {

struct GlobalBanRecord;
struct GlobalBanSummary;

/// Shows where a chatter has been banned, and what they had said at the time.
///
/// The card is deliberately evidence rather than a verdict: every entry names
/// the channel, the reason recorded with it and the messages that preceded it,
/// so that a moderator decides for themselves. A ban that has since been lifted
/// or vouched for is still listed, marked as such, rather than hidden — knowing
/// that a channel changed its mind is itself worth seeing.
class GlobalBanHistoryPopup final : public BasePopup
{
    Q_OBJECT

public:
    /// `channelId` is the channel being read, which decides what counts as
    /// "elsewhere" and which channel a vouch would apply to. `canVouch` is
    /// whether this viewer moderates it; the service checks this too, and this
    /// only decides whether to offer the button.
    GlobalBanHistoryPopup(QString offenderId, QString offenderName,
                          QString channelId, bool canVouch,
                          QWidget *parent = nullptr);

    /// Fetches the history and fills the card. Safe to call again to refresh.
    void load();

private:
    void showMessage(const QString &text);
    void showSummary(const GlobalBanSummary &summary);
    QWidget *buildRecordCard(const GlobalBanRecord &record);
    void vouch();

    QString offenderId_;
    QString offenderName_;
    QString channelId_;
    bool canVouch_ = false;

    QLabel *heading_ = nullptr;
    QVBoxLayout *entries_ = nullptr;
    QPushButton *vouchButton_ = nullptr;
};

}  // namespace chatterino
