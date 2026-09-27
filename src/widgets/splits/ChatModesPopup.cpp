// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/ChatModesPopup.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/commands/CommandController.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Theme.hpp"

#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int popupWidth = 260;

}  // namespace

namespace chatterino {

ChatModesPopup::ChatModesPopup(std::vector<ChatModeRow> rows, CommandSink send,
                              QWidget *parent)
    : BasePopup({BaseWindow::Frameless, BaseWindow::FramelessDraggable,
                 BaseWindow::DisableLayoutSave},
                parent)
    , send_(std::move(send))
{
    this->setWindowTitle(tr("Chat modes"));
    this->setFixedWidth(popupWidth);

    auto *root = new QVBoxLayout(this->getLayoutContainer());
    root->setSpacing(0);

    auto *heading = new QLabel(tr("CHAT MODES"), this);
    auto headingFont = heading->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 0.8);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    heading->setStyleSheet(
        QStringLiteral("color: %1")
            .arg(this->theme->messages.textColors.system.name()));
    root->addWidget(heading);

    this->rows_ = new QVBoxLayout();
    this->rows_->setSpacing(0);
    root->addLayout(this->rows_);

    for (const auto &row : rows)
    {
        this->addRow(row);
    }
}

void ChatModesPopup::addRow(const ChatModeRow &row)
{
    auto presets = chatModePresets(row.mode);

    auto *button = new QPushButton(this);
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);

    auto *layout = new QHBoxLayout(button);

    auto *label = new QLabel(chatModeLabel(row.mode), button);
    layout->addWidget(label);
    layout->addStretch(1);

    // What it is set to, so the state reads without switching anything on to
    // find out.
    if (!row.value.isEmpty())
    {
        auto *value = new QLabel(row.value, button);
        value->setStyleSheet(
            QStringLiteral("color: %1")
                .arg(this->theme->messages.textColors.system.name()));
        layout->addWidget(value);
    }

    if (!presets.empty())
    {
        // A chevron rather than a checkmark: these open a list instead of
        // toggling, and the two should not look alike.
        layout->addWidget(new QLabel(QStringLiteral("›"), button));
    }

    if (row.active)
    {
        auto labelFont = label->font();
        labelFont.setBold(true);
        label->setFont(labelFont);
        label->setStyleSheet(
            QStringLiteral("color: %1").arg(this->theme->accent.name()));
    }

    auto mode = row.mode;
    auto active = row.active;

    QObject::connect(button, &QPushButton::clicked, this, [this, mode, active,
                                                           presets] {
        if (presets.empty())
        {
            // On or off; clicking is the whole interaction.
            this->apply(mode, active ? -1 : 0);
            return;
        }

        QMenu menu;
        for (const auto &preset : presets)
        {
            auto amount = preset.amount;
            menu.addAction(preset.label, this, [this, mode, amount] {
                this->apply(mode, amount);
            });
        }
        menu.exec(QCursor::pos());
    });

    this->rows_->addWidget(button);
}

void ChatModesPopup::applyForTest(ChatMode mode, int amount)
{
    this->apply(mode, amount);
}

void ChatModesPopup::apply(ChatMode mode, int amount)
{
    if (this->send_)
    {
        this->send_(chatModeCommand(mode, amount));
    }

    this->close();
}

ChatModesPopup *ChatModesPopup::forChannel(const ChannelPtr &channel,
                                           TwitchChannel *twitch,
                                           QWidget *parent)
{
    std::vector<ChatModeRow> rows;
    if (twitch != nullptr)
    {
        auto modes = twitch->accessRoomModes();
        rows = chatModeRows(modes->followerOnly, modes->slowMode,
                            modes->submode, modes->emoteOnly, modes->r9k);
    }
    else
    {
        rows = chatModeRows(-1, 0, false, false, false);
    }

    return new ChatModesPopup(
        std::move(rows),
        [channel](const QString &command) {
            if (channel == nullptr)
            {
                return;
            }

            auto text = getApp()->getCommands()->execCommand(command, channel,
                                                             false);
            channel->sendMessage(text);
        },
        parent);
}

}  // namespace chatterino
