// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/RaidBannerWidget.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Theme.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <utility>

namespace {

using namespace chatterino;

constexpr int horizontalPadding = 8;
constexpr int verticalPadding = 4;

/// Calls the raid off through Helix, the same way /unraid does.
void cancelThroughHelix(TwitchChannel *channel)
{
    if (channel == nullptr || channel->roomId().isEmpty())
    {
        return;
    }

    auto id = channel->roomId();
    auto shared = channel->shared_from_this();

    getHelix()->cancelRaid(
        id,
        [shared] {
            if (auto *twitch = dynamic_cast<TwitchChannel *>(shared.get()))
            {
                twitch->setOutgoingRaid({});
            }
        },
        [shared](auto /*error*/, auto message) {
            // The banner stays up: the raid was not called off, and taking the
            // way out off screen because pressing it failed is the wrong
            // direction to fail in.
            shared->addSystemMessage(
                QCoreApplication::translate(
                    "RaidBannerWidget", "Could not cancel the raid - %1")
                    .arg(message));
        });
}

}  // namespace

namespace chatterino {

RaidBannerWidget::RaidBannerWidget(BaseWidget *parent)
    : RaidBannerWidget(parent, {})
{
}

RaidBannerWidget::RaidBannerWidget(BaseWidget *parent, CancelSink cancel)
    : BaseWidget(parent)
    , cancel_(std::move(cancel))
{
    this->setVisible(false);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->setAutoFillBackground(true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(horizontalPadding, verticalPadding,
                               horizontalPadding, verticalPadding);
    layout->setSpacing(horizontalPadding);

    this->label_ = new QLabel(this);
    this->cancelButton_ = new QPushButton(tr("Cancel"), this);
    this->cancelButton_->setCursor(Qt::PointingHandCursor);

    layout->addWidget(this->label_, 1);
    layout->addWidget(this->cancelButton_);

    QObject::connect(this->cancelButton_, &QPushButton::clicked, this, [this] {
        this->cancel();
    });

    this->tick_.setInterval(1000);
    QObject::connect(&this->tick_, &QTimer::timeout, this, [this] {
        this->refresh();
    });

    this->themeChangedEvent();
}

void RaidBannerWidget::setChannel(TwitchChannel *channel)
{
    if (channel == this->channel_)
    {
        return;
    }

    this->channel_ = channel;
    // Whatever was on screen belonged to the channel this split just left.
    this->channelHolder_ = {};

    if (channel != nullptr)
    {
        this->channelHolder_.managedConnect(channel->outgoingRaidChanged,
                                            [this] {
                                                this->refresh();
                                            });
    }

    this->refresh();
}

void RaidBannerWidget::refresh()
{
    if (this->label_ == nullptr || this->cancelButton_ == nullptr)
    {
        return;
    }

    auto raid = this->channel_ == nullptr ? std::nullopt
                                          : this->channel_->outgoingRaid();

    if (!raid.has_value())
    {
        this->tick_.stop();
        this->setVisible(false);
        this->updateGeometry();
        return;
    }

    this->label_->setText(
        raidBannerText(*raid, QDateTime::currentDateTime()));

    if (!this->tick_.isActive())
    {
        this->tick_.start();
    }

    if (!this->isVisible())
    {
        this->setVisible(true);
        this->updateGeometry();
    }
}

QString RaidBannerWidget::text() const
{
    if (this->label_ == nullptr || !this->isVisibleTo(this->parentWidget()))
    {
        return {};
    }

    return this->label_->text();
}

void RaidBannerWidget::cancel()
{
    if (this->cancel_)
    {
        this->cancel_();
        return;
    }

    cancelThroughHelix(this->channel_);
}

void RaidBannerWidget::cancelForTest()
{
    this->cancel();
}

void RaidBannerWidget::themeChangedEvent()
{
    auto palette = this->palette();
    palette.setColor(QPalette::Window, this->theme->splits.header.background);
    palette.setColor(QPalette::WindowText,
                     this->theme->messages.textColors.regular);
    this->setPalette(palette);
}

}  // namespace chatterino
