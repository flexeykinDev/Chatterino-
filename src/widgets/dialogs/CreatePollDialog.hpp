// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BasePopup.hpp"

#include <QString>
#include <QStringList>

#include <chrono>
#include <memory>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

namespace chatterino {

class Channel;
using ChannelPtr = std::shared_ptr<Channel>;

/// What a filled-in poll dialog amounts to.
///
/// Separated from the widgets so the rules about what Twitch will accept can
/// be checked without building a window.
struct PollDraft {
    QString title;
    QStringList choices;
    std::chrono::seconds duration{0};
    int pointsPerVote = 0;

    /// Twitch's limits, which are worth enforcing here rather than sending a
    /// request that comes back as an error with no field named.
    static constexpr int maxTitleLength = 60;
    static constexpr int minChoices = 2;
    static constexpr int maxChoices = 5;
    static constexpr int maxChoiceLength = 25;
    static constexpr int minDurationSeconds = 15;
    static constexpr int maxDurationSeconds = 1800;

    /// Why this cannot be sent yet, or empty when it can.
    ///
    /// Says what to fix rather than that something is wrong: "a poll needs at
    /// least two choices" beats a disabled button with no explanation.
    [[nodiscard]] QString problem() const;

    /// Choices with nothing in them, which the form always has a couple of.
    [[nodiscard]] QStringList filledChoices() const;
};

/// Builds a poll without writing out its arguments.
///
/// `/poll --title "..." --duration 2m --choice "..." --choice "..."` is the
/// only way to start one otherwise, which is a lot of punctuation to get right
/// in a chat box for something with four fields.
class CreatePollDialog final : public BasePopup
{
    Q_OBJECT

public:
    explicit CreatePollDialog(ChannelPtr channel, QWidget *parent = nullptr);

private:
    void addChoiceRow(const QString &text = {});
    void removeChoiceRow(QLineEdit *edit);
    [[nodiscard]] PollDraft draft() const;
    void refreshValidity();
    void submit();

    ChannelPtr channel_;

    QLineEdit *title_ = nullptr;
    QSpinBox *durationMinutes_ = nullptr;
    QSpinBox *durationSeconds_ = nullptr;
    QSpinBox *points_ = nullptr;
    QVBoxLayout *choiceRows_ = nullptr;
    std::vector<QLineEdit *> choices_;
    QPushButton *addChoice_ = nullptr;
    QPushButton *create_ = nullptr;
    QLabel *problem_ = nullptr;
};

}  // namespace chatterino
