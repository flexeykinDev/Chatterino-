// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/twitch/OutgoingRaid.hpp"
#include "widgets/BaseWidget.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QTimer>

#include <functional>
#include <optional>

class QLabel;
class QPushButton;

namespace chatterino {

class TwitchChannel;

/// The raid this channel has started, counting down until it goes out.
///
/// Twitch gives the broadcaster a window to call a raid off, and nothing in
/// the client said so: `/raid` returned in silence and the next thing that
/// happened was the audience leaving. This says what is about to happen and
/// offers the way out while there still is one.
///
/// Built from real widgets rather than painted like the poll banner beside it,
/// because this one has a button on it. A painted button means answering
/// "was that click on me" by hand, which is how the chat modes panel ended up
/// unclickable.
class RaidBannerWidget : public BaseWidget
{
    Q_OBJECT

public:
    /// Calls the raid off. Passed in rather than reached for, so the banner
    /// can be tested without Twitch to talk to.
    using CancelSink = std::function<void()>;

    explicit RaidBannerWidget(BaseWidget *parent);
    RaidBannerWidget(BaseWidget *parent, CancelSink cancel);

    /// Follows a channel's raids, or none.
    void setChannel(TwitchChannel *channel);

    /// What the banner says, empty when it is not showing.
    [[nodiscard]] QString text() const;

    /// Presses Cancel, for a test.
    void cancelForTest();

protected:
    void themeChangedEvent() override;

private:
    void refresh();
    void cancel();

    TwitchChannel *channel_ = nullptr;
    CancelSink cancel_;

    QLabel *label_ = nullptr;
    QPushButton *cancelButton_ = nullptr;

    /// Redraws the countdown once a second, and takes the banner down when it
    /// runs out. Stopped whenever there is no raid, so an idle split is not
    /// waking up every second for nothing.
    QTimer tick_;

    pajlada::Signals::SignalHolder channelHolder_;
};

}  // namespace chatterino
