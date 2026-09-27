// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/twitch/Mentions.hpp"
#include "widgets/BasePopup.hpp"

#include <pajlada/signals/signalholder.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;

namespace chatterino {

class Channel;
using ChannelPtr = std::shared_ptr<Channel>;
class ChannelView;

/// Every mention, in one window, with a box to answer the one you pick.
///
/// Upstream collects mentions into a channel already, but the only way to look
/// at them is to give up a split to them permanently, and a reply typed there
/// goes to whichever channel the split is on rather than the one the mention
/// came from. This shows the list without spending a split, and sends a reply
/// back where it belongs.
class MentionsPopup final : public BasePopup
{
    Q_OBJECT

public:
    /// Sends a reply. Passed in rather than reached for, so a test can check
    /// what the popup asked to send, to whom, without a Twitch connection.
    using ReplySink = std::function<void(const MessagePtr &,
                                         const MentionReplyTarget &,
                                         const QString &)>;

    MentionsPopup(ChannelPtr mentions, ChannelIsOpen channelIsOpen,
                  ReplySink send, QWidget *parent = nullptr);

    /// Shows the window, building it the first time and raising it after.
    ///
    /// One window rather than one per caller: two lists of the same mentions
    /// that disagree about what has been read is worse than none.
    static void showUnique(QWidget *parent);

    /// How many mentions have arrived that have not been looked at.
    static int unreadCount();

    /// Which mention the reply box is aimed at, or null when none of them can
    /// be answered.
    [[nodiscard]] MessagePtr currentTarget() const;

    /// Steps through the answerable mentions; negative goes to older ones.
    void step(int delta);

    /// Sends what is in the reply box, for a test. Returns false when there was
    /// nothing to send.
    bool sendForTest(const QString &text);

    /// What the row above the reply box says — the mention being answered, or
    /// why none of them can be.
    [[nodiscard]] QString statusText() const;

    /// Records everything currently listed as read.
    void markRead();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void rebuildTargets();
    void refreshStatus();
    void send();

    ChannelPtr mentions_;
    ChannelIsOpen channelIsOpen_;
    ReplySink send_;

    /// The answerable mentions, newest first. Rebuilt whenever one arrives,
    /// because a mention going stale or its channel closing changes the list.
    std::vector<MessagePtr> replyable_;
    std::size_t index_ = 0;

    ChannelView *view_ = nullptr;
    QLabel *status_ = nullptr;
    QLineEdit *input_ = nullptr;
    QPushButton *reply_ = nullptr;
    QPushButton *older_ = nullptr;
    QPushButton *newer_ = nullptr;

    pajlada::Signals::SignalHolder connections_;
};

}  // namespace chatterino
