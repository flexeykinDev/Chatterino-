// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace chatterino {

/// Why the companion features are or are not doing anything.
enum class CompanionStatus {
    /// No address configured. This is the default and not a fault.
    Disabled,
    /// Configured, but nobody is signed in. Every route needs the caller's own
    /// Twitch token, so an anonymous client can ask for nothing.
    SignedOut,
    /// Configured and signed in, but nothing has been asked yet.
    Unknown,
    /// The last thing asked was answered.
    Reachable,
    /// Repeated requests have failed.
    Unreachable,
};

/// Tracks whether the companion service is answering.
///
/// This exists because of a specific failure that is invisible without it:
/// when the service is down, every chatter comes back unmarked, which looks
/// exactly like a chat where nobody has been banned anywhere. Nothing in the
/// window distinguishes "the feature is off" from "the feature is broken", and
/// a person has no way to tell which they are looking at.
///
/// A single failure does not count as down. A request lost to a dropped
/// connection or a sleeping laptop is ordinary, and a status that flickers on
/// one is worse than no status at all.
class CompanionHealth
{
public:
    /// Consecutive failures before the service is called unreachable.
    static constexpr int failuresBeforeUnreachable = 2;

    /// Whether an address is set and somebody is signed in. Both are
    /// preconditions rather than faults, and each has its own explanation.
    /// Returns whether the status changed.
    bool setAvailability(bool configured, bool signedIn);

    /// Returns whether the status changed, so a caller can avoid redrawing
    /// for an answer that told it nothing new.
    bool recordSuccess();
    bool recordFailure();

    [[nodiscard]] CompanionStatus status() const;

private:
    [[nodiscard]] CompanionStatus compute() const;

    bool configured_ = false;
    bool signedIn_ = false;
    int consecutiveFailures_ = 0;
    bool everSucceeded_ = false;
};

/// A sentence explaining the status, for showing beside the address setting.
QString describeCompanionStatus(CompanionStatus status);

}  // namespace chatterino
