// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/PollBannerWidget.hpp"

#include "Application.hpp"
#include "providers/twitch/PubSubManager.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Theme.hpp"

#include <QPainter>

namespace {

/// Heights, before scaling. The bar is deliberately thin: this sits above a
/// chat somebody is reading, and a poll is not the reason they opened it.
constexpr int titleHeight = 20;
constexpr int rowHeight = 18;
constexpr int verticalPadding = 6;
constexpr int horizontalPadding = 8;
constexpr int barCornerRadius = 3;

/// The share column is fixed so the bars all start at the same place; ragged
/// left edges make two similar numbers hard to compare at a glance.
constexpr int shareColumnWidth = 40;
constexpr int countdownWidth = 44;

}  // namespace

namespace chatterino {

PollBannerWidget::PollBannerWidget(BaseWidget *parent)
    : BaseWidget(parent)
{
    this->setVisible(false);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    this->tick_.setInterval(500);
    QObject::connect(&this->tick_, &QTimer::timeout, this, [this] {
        if (this->hideAt_.isValid() &&
            QDateTime::currentDateTime() >= this->hideAt_)
        {
            this->hidePoll();
            return;
        }

        // The countdown is the only thing that changes between frames while a
        // poll runs, so a repaint is enough; nothing is re-laid out.
        this->update();
    });

    // Absent in an application built without one, where reaching through it
    // is an access violation rather than a failed assertion once asserts are
    // compiled out. A banner with nothing to listen to simply never shows.
    auto *pubSub = getApp()->getTwitchPubSub();
    if (pubSub != nullptr)
    {
        this->signalHolder_.managedConnect(
            pubSub->polls.updated,
            [this](const QString &channelId, const Poll &poll) {
                if (channelId == this->roomId_)
                {
                    this->onPoll(poll);
                }
            });
    }
}

void PollBannerWidget::setChannel(TwitchChannel *channel)
{
    QString roomId;
    if (channel != nullptr)
    {
        roomId = channel->roomId();

        // A channel does not know its own id when the split is first given
        // it; that arrives with ROOMSTATE a moment later. Reading it once and
        // keeping the empty string means no frame ever matches and the banner
        // never appears, which is exactly how this shipped the first time.
        if (roomId.isEmpty())
        {
            this->channelHolder_ = {};
            this->channelHolder_.managedConnect(channel->roomIdSet,
                                                [this, channel] {
                                                    this->setChannel(channel);
                                                });
        }
    }

    if (roomId == this->roomId_)
    {
        return;
    }

    this->roomId_ = roomId;

    // Whatever was on screen belonged to the channel this split just left.
    this->hidePoll();
}

void PollBannerWidget::onPoll(const Poll &poll)
{
    if (poll.isRunning())
    {
        this->poll_ = poll;
        this->hideAt_ = {};
        this->tick_.start();
        this->refresh();
        return;
    }

    if (!poll.hasShowableResult())
    {
        this->hidePoll();
        return;
    }

    // Finished, with a result worth seeing. Keep it up briefly rather than
    // taking it down at the moment it becomes interesting.
    auto alreadyEnding = this->hideAt_.isValid();
    this->poll_ = poll;
    if (!alreadyEnding)
    {
        this->hideAt_ = QDateTime::currentDateTime().addMSecs(holdResultMs);
    }
    this->tick_.start();
    this->refresh();
}

void PollBannerWidget::hidePoll()
{
    this->tick_.stop();
    this->poll_.reset();
    this->hideAt_ = {};
    this->setVisible(false);
    this->updateGeometry();
}

void PollBannerWidget::refresh()
{
    this->setVisible(this->poll_.has_value());
    this->updateGeometry();
    this->update();
}

QSize PollBannerWidget::sizeHint() const
{
    if (!this->poll_)
    {
        return {0, 0};
    }

    auto scale = this->scale();
    auto rows = static_cast<int>(this->poll_->choices.size());

    return {0, static_cast<int>((titleHeight + rows * rowHeight +
                                 verticalPadding * 2) *
                                scale)};
}

void PollBannerWidget::paintEvent(QPaintEvent * /*event*/)
{
    if (!this->poll_)
    {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    auto scale = this->scale();
    auto *theme = this->theme;
    const auto &poll = *this->poll_;

    painter.fillRect(this->rect(), theme->splits.header.background);

    auto pad = static_cast<int>(horizontalPadding * scale);
    auto y = static_cast<int>(verticalPadding * scale);
    auto width = this->width() - pad * 2;

    // Title, with the countdown pinned to the right so it does not shift
    // about as the title changes length.
    painter.setPen(theme->messages.textColors.regular);
    painter.setFont(getApp()->getFonts()->getFont(FontStyle::ChatMediumBold,
                                                  scale));

    auto countdown = static_cast<int>(countdownWidth * scale);
    auto titleRect =
        QRect(pad, y, width - countdown, static_cast<int>(titleHeight * scale));
    painter.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(
                         poll.title, Qt::ElideRight, titleRect.width()));

    auto remaining = poll.secondsRemaining(QDateTime::currentDateTime());
    auto status = poll.isRunning()
                      ? QStringLiteral("%1:%2")
                            .arg(remaining / 60)
                            .arg(remaining % 60, 2, 10, QLatin1Char('0'))
                      : tr("done");

    painter.setPen(theme->messages.textColors.system);
    painter.drawText(QRect(this->width() - pad - countdown, y, countdown,
                           static_cast<int>(titleHeight * scale)),
                     Qt::AlignVCenter | Qt::AlignRight, status);

    y += static_cast<int>(titleHeight * scale);

    auto shares = pollPercentages(poll.choices);
    const auto *leader = poll.leader();

    painter.setFont(getApp()->getFonts()->getFont(FontStyle::ChatMedium, scale));

    for (std::size_t i = 0; i < poll.choices.size(); i++)
    {
        const auto &choice = poll.choices[i];
        auto share = shares[i];
        auto row = static_cast<int>(rowHeight * scale);
        auto shareColumn = static_cast<int>(shareColumnWidth * scale);

        painter.setPen(theme->messages.textColors.regular);
        painter.drawText(QRect(pad, y, shareColumn, row),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         QStringLiteral("%1%").arg(share));

        auto barLeft = pad + shareColumn;
        auto barWidth = width - shareColumn;
        auto barRect = QRectF(barLeft, y + row * 0.15, barWidth, row * 0.7);

        painter.setPen(Qt::NoPen);
        painter.setBrush(theme->messages.backgrounds.alternate);
        painter.drawRoundedRect(barRect, barCornerRadius * scale,
                                barCornerRadius * scale);

        if (share > 0)
        {
            auto filled = barRect;
            filled.setWidth(barRect.width() * share / 100.0);

            // The leader is picked out so a glance reads the outcome without
            // comparing numbers. A tie has no leader and nothing is picked.
            painter.setBrush(&choice == leader ? theme->accent
                                               : theme->messages.disabled);
            painter.drawRoundedRect(filled, barCornerRadius * scale,
                                    barCornerRadius * scale);
        }

        painter.setPen(theme->messages.textColors.regular);
        painter.drawText(
            barRect.adjusted(pad, 0, -pad, 0), Qt::AlignVCenter | Qt::AlignLeft,
            painter.fontMetrics().elidedText(
                choice.title, Qt::ElideRight,
                static_cast<int>(barRect.width()) - pad * 2));

        y += row;
    }
}

void PollBannerWidget::themeChangedEvent()
{
    this->update();
}

void PollBannerWidget::scaleChangedEvent(float /*scale*/)
{
    this->updateGeometry();
    this->update();
}

}  // namespace chatterino
