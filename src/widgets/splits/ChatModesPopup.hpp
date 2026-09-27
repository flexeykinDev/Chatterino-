// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/twitch/ChatModes.hpp"
#include "widgets/BasePopup.hpp"

#include <functional>
#include <memory>

class QVBoxLayout;

namespace chatterino {

class Channel;
using ChannelPtr = std::shared_ptr<Channel>;
class TwitchChannel;

/// The chat modes of a channel, as a panel rather than a context menu.
///
/// Each row says what the mode is and what it is currently set to, so the
/// state is readable without switching anything on to find out. The two modes
/// that take a duration open a list of the usual ones instead of asking for a
/// number: setting slow mode should not involve typing into a box.
class ChatModesPopup final : public BasePopup
{
    Q_OBJECT

public:
    /// `send` runs a chat command. Passed in rather than reached for, so the
    /// popup can be opened in a test without a channel to talk to.
    using CommandSink = std::function<void(const QString &)>;

    ChatModesPopup(std::vector<ChatModeRow> rows, CommandSink send,
                   QWidget *parent = nullptr);

    /// Builds one for a channel, wired to send its commands.
    static ChatModesPopup *forChannel(const ChannelPtr &channel,
                                      TwitchChannel *twitch, QWidget *parent);

    /// Applies a mode, for a test that checks which command a row sends.
    void applyForTest(ChatMode mode, int amount);

private:
    void addRow(const ChatModeRow &row);
    void apply(ChatMode mode, int amount);

    CommandSink send_;
    QVBoxLayout *rows_ = nullptr;
};

}  // namespace chatterino
