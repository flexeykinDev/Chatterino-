// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QtGlobal>

namespace chatterino {

/// Decides which of a room's backlog has already been shown.
///
/// A room is backfilled every time it is joined, and it is rejoined on every
/// reconnect — which happens on its own, with backoff, whenever the service
/// restarts. Without this the entire conversation reappears underneath itself,
/// and it looks like everyone said everything twice.
///
/// Own messages are the awkward case. They are shown the moment they are typed,
/// before the server has given them an id, so the id has to come from the
/// acknowledgement instead — otherwise a reconnect brings them back as history
/// that this has never seen.
class ShadowBacklog
{
public:
    /// Whether a message from the backlog is new. Messages with no id are
    /// always new: only the server assigns ids, so an id of zero is a local
    /// echo that cannot have been seen before.
    [[nodiscard]] bool isNew(qint64 id) const;

    /// Records that a message has been shown, from wherever it came.
    void seen(qint64 id);

    [[nodiscard]] qint64 highestSeen() const;

private:
    qint64 highestSeen_ = 0;
};

}  // namespace chatterino
