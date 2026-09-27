// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/twitch/PollState.hpp"
#include "widgets/BaseWidget.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QTimer>

#include <memory>
#include <optional>

namespace chatterino {

class TwitchChannel;

/// The poll running on a channel, shown above its chat.
///
/// Hidden whenever there is nothing to show, which is nearly always, so it
/// costs a stream no room at all until a poll starts.
///
/// A finished poll lingers briefly rather than vanishing the instant voting
/// closes: the result is the part people actually want to see, and it would
/// otherwise disappear at exactly the moment it became interesting.
class PollBannerWidget : public BaseWidget
{
    Q_OBJECT

public:
    /// How long a finished poll's result stays up.
    static constexpr int holdResultMs = 10000;

    explicit PollBannerWidget(BaseWidget *parent);

    /// Follows a channel's polls, or none. Matches the pinned banner beside
    /// it, which also takes the Twitch channel directly.
    void setChannel(TwitchChannel *channel);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    void themeChangedEvent() override;
    void scaleChangedEvent(float scale) override;

private:
    void onPoll(const Poll &poll);
    void hidePoll();
    /// Recomputes the height this needs and asks the layout for it.
    void refresh();

    QString roomId_;
    std::optional<Poll> poll_;

    /// Redraws the countdown, and takes a finished poll down once its result
    /// has had its moment.
    QTimer tick_;
    /// When the result stops being shown. Unset while a poll is running.
    QDateTime hideAt_;

    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace chatterino
