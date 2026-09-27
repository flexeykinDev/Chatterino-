// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/TypingNotifier.hpp"

namespace chatterino {

TypingNotifier::Decision TypingNotifier::inputChanged(bool hasText,
                                                      qint64 nowMs)
{
    if (!hasText)
    {
        if (!this->typing_)
        {
            return std::nullopt;
        }

        this->typing_ = false;
        return false;
    }

    if (!this->typing_)
    {
        this->typing_ = true;
        this->lastSentAtMs_ = nowMs;
        return true;
    }

    // Already typing, and the server has been told recently enough. Every
    // keystroke between repeats is silence, which is the whole point.
    if (nowMs - this->lastSentAtMs_ < repeatAfterMs)
    {
        return std::nullopt;
    }

    this->lastSentAtMs_ = nowMs;
    return true;
}

TypingNotifier::Decision TypingNotifier::messageSent(qint64 nowMs)
{
    if (!this->typing_)
    {
        return std::nullopt;
    }

    this->typing_ = false;
    this->lastSentAtMs_ = nowMs;
    return false;
}

void TypingNotifier::reset()
{
    this->typing_ = false;
    this->lastSentAtMs_ = 0;
}

bool TypingNotifier::isTyping() const
{
    return this->typing_;
}

}  // namespace chatterino
