// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

namespace pajlada::Signals {
class SignalHolder;
}  // namespace pajlada::Signals

namespace chatterino {

class LabelButton;

/// Wires up the "@" button that opens the mentions window.
///
/// Shared because the button lives in two different places depending on the
/// window frame: beside the tabs normally, and in the title bar on Windows when
/// Chatterino draws its own. Wiring it in only one of them makes it invisible on
/// half the installs, which is exactly what happened.
///
/// The `relayout` function gets called whenever the button's width changes —
/// when the count appears, or gains or loses a digit — so whatever is beside it
/// can move out of the way.
void initMentionsButton(LabelButton &button,
                        const std::function<void()> &relayout,
                        pajlada::Signals::SignalHolder &signalHolder);

}  // namespace chatterino
