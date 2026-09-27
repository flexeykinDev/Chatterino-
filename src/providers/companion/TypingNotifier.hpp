// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <optional>

namespace chatterino {

/// Decides when to tell the server that somebody is typing.
///
/// The input box reports every keystroke, which is far more often than anyone
/// needs to be told. This turns that stream into the few frames that actually
/// carry information: one when typing starts, an occasional repeat so the
/// indicator does not expire mid-sentence, and one when the box empties or the
/// message goes.
///
/// The clock is passed in rather than read, so the timing rules can be tested
/// without waiting for real seconds to pass.
class TypingNotifier
{
public:
    /// How long the server keeps an indicator alive without hearing again.
    /// Repeats go out comfortably inside it rather than at the edge, since a
    /// frame that arrives late is the same as one that never came.
    static constexpr qint64 repeatAfterMs = 4000;

    /// What, if anything, to send. Nothing means the server already knows.
    using Decision = std::optional<bool>;

    /// The input box's contents changed. `hasText` is whether anything is left
    /// in it.
    [[nodiscard]] Decision inputChanged(bool hasText, qint64 nowMs);

    /// The message was sent. The server stops showing the indicator when the
    /// message arrives, but it has no way to know about a message sent
    /// somewhere else, so say so explicitly.
    [[nodiscard]] Decision messageSent(qint64 nowMs);

    /// The channel was left, or the connection dropped. Forgets everything
    /// without sending: there is nobody to tell.
    void reset();

    [[nodiscard]] bool isTyping() const;

private:
    bool typing_ = false;
    qint64 lastSentAtMs_ = 0;
};

}  // namespace chatterino
