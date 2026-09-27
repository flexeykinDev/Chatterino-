// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/GlobalBanHistoryPopup.hpp"

#include "Application.hpp"
#include "providers/companion/CompanionController.hpp"
#include "providers/companion/GlobalBan.hpp"

#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <utility>

namespace {

/// Wide enough for a sentence of chat, tall enough for two or three entries
/// before it scrolls.
constexpr int popupWidth = 460;
constexpr int popupHeight = 420;

}  // namespace

namespace chatterino {

GlobalBanHistoryPopup::GlobalBanHistoryPopup(QString offenderId,
                                             QString offenderName,
                                             QString channelId, bool canVouch,
                                             QWidget *parent)
    : BasePopup({BaseWindow::EnableCustomFrame, BaseWindow::Dialog}, parent)
    , offenderId_(std::move(offenderId))
    , offenderName_(std::move(offenderName))
    , channelId_(std::move(channelId))
    , canVouch_(canVouch)
{
    this->setWindowTitle(tr("Ban history"));
    this->resize(popupWidth, popupHeight);

    auto *root = new QVBoxLayout(this->getLayoutContainer());

    this->heading_ = new QLabel(this);
    this->heading_->setWordWrap(true);
    root->addWidget(this->heading_);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    this->entries_ = new QVBoxLayout(content);
    this->entries_->setContentsMargins(0, 0, 0, 0);
    this->entries_->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    if (this->canVouch_)
    {
        this->vouchButton_ = new QPushButton(this);
        this->vouchButton_->setToolTip(
            tr("Hides this marker for everyone reading this channel, and only "
               "this channel. It does not undo any ban."));
        QObject::connect(this->vouchButton_, &QPushButton::clicked, this,
                         [this] {
                             this->toggleVouch();
                         });
        root->addWidget(this->vouchButton_);
        this->updateVouchButton();
    }

    this->load();
}

void GlobalBanHistoryPopup::load()
{
    this->showMessage(tr("Loading…"));

    auto *companion = getApp()->getCompanion();
    if (companion == nullptr || !companion->isEnabled())
    {
        this->showMessage(
            tr("No companion service is configured, so there is no ban "
               "history to read."));
        return;
    }

    // The request outlives the window if it is closed while in flight, so the
    // callback has to check that there is still something to fill.
    QPointer<GlobalBanHistoryPopup> self(this);
    companion->fetchHistory(
        this->offenderId_, this->channelId_,
        [self](std::optional<GlobalBanSummary> summary) {
            if (self.isNull())
            {
                return;
            }

            if (!summary)
            {
                self->showMessage(
                    tr("Could not reach the companion service."));
                return;
            }

            self->showSummary(*summary);
        });
}

void GlobalBanHistoryPopup::showMessage(const QString &text)
{
    this->heading_->setText(text);

    // Remove everything but the trailing stretch.
    while (this->entries_->count() > 1)
    {
        auto *item = this->entries_->takeAt(0);
        if (item->widget() != nullptr)
        {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

void GlobalBanHistoryPopup::showSummary(const GlobalBanSummary &summary)
{
    this->showMessage(QString{});

    this->vouched_ = summary.vouched;
    this->updateVouchButton();

    if (summary.history.empty())
    {
        this->heading_->setText(
            tr("%1 has no recorded bans.").arg(this->offenderName_));
        return;
    }

    if (summary.vouched)
    {
        // The marker is hidden here, so the count would read as zero and say
        // nothing useful. What matters is that this channel made a decision.
        this->heading_->setText(
            //: %1 is a chatter's name. Shown when this channel has vouched for
            //: them, so their bans elsewhere raise no marker here.
            tr("This channel vouches for %1. Their record elsewhere is below.")
                .arg(this->offenderName_));
    }
    else
    {
        this->heading_->setText(
            //: %1 is a chatter's name, %n the number of channels they are
            //: still banned on as seen from the channel being read.
            tr("%1 is banned on %n other channel(s).", "", summary.markerCount)
                .arg(this->offenderName_));
    }

    for (const auto &record : summary.history)
    {
        this->entries_->insertWidget(this->entries_->count() - 1,
                                     this->buildRecordCard(record));
    }
}

QWidget *GlobalBanHistoryPopup::buildRecordCard(const GlobalBanRecord &record)
{
    auto *card = new QFrame(this);
    card->setFrameShape(QFrame::StyledPanel);

    auto *layout = new QVBoxLayout(card);

    auto when = QLocale().toString(record.bannedAt, QLocale::ShortFormat);

    QString status;
    if (record.isLifted())
    {
        status = tr("no longer in force");
    }

    auto headline = status.isEmpty()
                        ? tr("%1 · %2").arg(record.channelLogin, when)
                        : tr("%1 · %2 · %3")
                              .arg(record.channelLogin, when, status);

    auto *title = new QLabel(headline, card);
    title->setWordWrap(true);
    layout->addWidget(title);

    if (!record.reason.isEmpty())
    {
        auto *reason = new QLabel(tr("Reason: %1").arg(record.reason), card);
        reason->setWordWrap(true);
        layout->addWidget(reason);
    }

    for (const auto &line : record.context)
    {
        // Chat text is shown as plain text, never rich text: a message is
        // whatever the chatter typed, including things Qt would read as markup.
        auto *body = new QLabel(line.body, card);
        body->setTextFormat(Qt::PlainText);
        body->setWordWrap(true);
        layout->addWidget(body);
    }

    return card;
}

void GlobalBanHistoryPopup::updateVouchButton()
{
    if (this->vouchButton_ == nullptr)
    {
        return;
    }

    this->vouchButton_->setText(this->vouched_
                                    ? tr("Withdraw this channel's vouch")
                                    : tr("Vouch for them in this channel"));
}

void GlobalBanHistoryPopup::toggleVouch()
{
    auto *companion = getApp()->getCompanion();
    if (companion == nullptr)
    {
        return;
    }

    if (this->vouchButton_ != nullptr)
    {
        this->vouchButton_->setEnabled(false);
    }

    auto granting = !this->vouched_;

    QPointer<GlobalBanHistoryPopup> self(this);
    auto done = [self, granting](bool ok) {
        if (self.isNull())
        {
            return;
        }

        if (self->vouchButton_ != nullptr)
        {
            self->vouchButton_->setEnabled(true);
        }

        if (ok)
        {
            self->vouched_ = granting;
            // Re-read rather than assume: the service is what decides, and it
            // may have been told something else in the meantime.
            self->load();
        }
    };

    if (granting)
    {
        companion->vouch(this->offenderId_, this->channelId_, done);
    }
    else
    {
        companion->withdrawVouch(this->offenderId_, this->channelId_, done);
    }
}

}  // namespace chatterino
