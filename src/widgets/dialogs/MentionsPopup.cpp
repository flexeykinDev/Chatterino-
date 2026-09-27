// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/MentionsPopup.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/commands/CommandController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageElement.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "widgets/helper/ChannelView.hpp"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QFontMetrics>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

using namespace chatterino;

constexpr int popupWidth = 520;
constexpr int popupHeight = 620;

/// The mentions channel, or null when there is no application around — which
/// is every unit test that does not ask for one.
ChannelPtr mentionsChannelOrNull()
{
    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return {};
    }

    auto *twitch = app->getTwitch();
    if (twitch == nullptr)
    {
        return {};
    }

    return twitch->getMentionsChannel();
}

/// Whether a channel is currently joined, by asking the application.
bool channelIsJoined(const QString &name)
{
    if (name.isEmpty())
    {
        return false;
    }

    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return false;
    }

    auto *twitch = app->getTwitch();
    if (twitch == nullptr)
    {
        return false;
    }

    return !twitch->getChannelOrEmpty(name)->isEmpty();
}

/// Sends a reply into the channel the mention came from.
void sendReplyToChannel(const MessagePtr &message,
                        const MentionReplyTarget &target, const QString &text)
{
    auto *app = tryGetApp();
    if (app == nullptr || app->getTwitch() == nullptr)
    {
        return;
    }

    auto channel = app->getTwitch()->getChannelOrEmpty(target.channelName);
    auto *twitch = dynamic_cast<TwitchChannel *>(channel.get());
    if (twitch == nullptr)
    {
        return;
    }

    // So the reply shows as part of a thread locally too, rather than only once
    // Twitch echoes it back with the reply tags on it.
    if (message != nullptr && message->replyThread == nullptr)
    {
        twitch->getOrCreateThread(message);
    }

    auto *commands = app->getCommands();
    auto outgoing =
        commands == nullptr ? text : commands->execCommand(text, channel, false);

    twitch->sendReply(outgoing, target.messageId);
}

}  // namespace

namespace chatterino {

MentionsPopup::MentionsPopup(ChannelPtr mentions, ChannelIsOpen channelIsOpen,
                             ReplySink send, QWidget *parent)
    : BasePopup({BaseWindow::DisableLayoutSave, BaseWindow::BoundsCheckOnShow},
                parent)
    , mentions_(std::move(mentions))
    , channelIsOpen_(std::move(channelIsOpen))
    , send_(std::move(send))
{
    this->setWindowTitle(tr("Mentions"));
    this->resize(popupWidth, popupHeight);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    this->view_ = new ChannelView(this, ChannelView::Context::None,
                                  getSettings()->scrollbackSplitLimit);
    root->addWidget(this->view_, 1);

    auto *replyRow = new QVBoxLayout();
    replyRow->setContentsMargins(8, 8, 8, 8);
    replyRow->setSpacing(6);

    auto *statusRow = new QHBoxLayout();
    statusRow->setSpacing(4);

    this->older_ = new QPushButton(QStringLiteral("‹"), this);
    this->older_->setFixedWidth(28);
    this->older_->setToolTip(tr("An earlier mention"));
    this->newer_ = new QPushButton(QStringLiteral("›"), this);
    this->newer_->setFixedWidth(28);
    this->newer_->setToolTip(tr("A later mention"));

    this->status_ = new QLabel(this);
    this->status_->setWordWrap(false);
    // Elide rather than wrap: a long message must not make the window taller
    // every time a different mention is selected.
    this->status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    this->status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    statusRow->addWidget(this->older_);
    statusRow->addWidget(this->newer_);
    statusRow->addWidget(this->status_, 1);
    replyRow->addLayout(statusRow);

    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(6);

    this->input_ = new QLineEdit(this);
    this->input_->setPlaceholderText(tr("Reply to this mention"));
    this->reply_ = new QPushButton(tr("Reply"), this);
    this->reply_->setDefault(true);

    inputRow->addWidget(this->input_, 1);
    inputRow->addWidget(this->reply_);
    replyRow->addLayout(inputRow);

    root->addLayout(replyRow);
    this->setLayout(root);

    QObject::connect(this->older_, &QPushButton::clicked, this, [this] {
        this->step(-1);
    });
    QObject::connect(this->newer_, &QPushButton::clicked, this, [this] {
        this->step(1);
    });
    QObject::connect(this->reply_, &QPushButton::clicked, this, [this] {
        this->send();
    });
    QObject::connect(this->input_, &QLineEdit::returnPressed, this, [this] {
        this->send();
    });

    // Only now that every widget the refresh touches exists. Doing this from
    // the middle of building the window has dereferenced widgets that were not
    // there yet more than once in this codebase.
    if (this->mentions_ != nullptr)
    {
        this->view_->setChannel(this->mentions_);
        this->view_->setSourceChannel(this->mentions_);

        // Without this the view does not say which channel each mention came
        // from, which is most of what makes a combined list readable. The flag
        // is normally set only for a view that belongs to a split.
        auto flags = this->view_->getFlags();
        flags.set(MessageElementFlag::ChannelName);
        flags.unset(MessageElementFlag::ModeratorTools);
        this->view_->setOverrideFlags(flags);

        this->connections_.managedConnect(
            this->mentions_->messageAppended,
            [this](auto &, auto) {
                this->rebuildTargets();

                // Looking at the list is what reading them means, so one
                // arriving while this window is in front must not leave a
                // count sitting on a button beside a window showing it.
                if (this->isVisible() && this->isActiveWindow())
                {
                    this->markRead();
                }
            });
    }

    this->rebuildTargets();
    this->input_->setFocus();
}

void MentionsPopup::showUnique(QWidget *parent)
{
    static QPointer<MentionsPopup> popup;

    if (popup.isNull())
    {
        popup = new MentionsPopup(mentionsChannelOrNull(), channelIsJoined,
                                  sendReplyToChannel, parent);
        popup->setAttribute(Qt::WA_DeleteOnClose);
    }

    popup->show();
    popup->raise();
    popup->activateWindow();
    popup->markRead();
}

int MentionsPopup::unreadCount()
{
    auto mentions = mentionsChannelOrNull();
    if (mentions == nullptr)
    {
        return 0;
    }

    return unreadMentions(mentions->getMessageSnapshot(),
                          getSettings()->lastSeenMention.getValue());
}

void MentionsPopup::showEvent(QShowEvent *event)
{
    BasePopup::showEvent(event);
    // Reopening it is how you catch up, so what is on screen counts as read.
    this->markRead();
}

void MentionsPopup::markRead()
{
    if (this->mentions_ == nullptr)
    {
        return;
    }

    auto newest = newestMentionId(this->mentions_->getMessageSnapshot());
    if (!newest.isEmpty())
    {
        getSettings()->lastSeenMention.setValue(newest);
    }
}

void MentionsPopup::rebuildTargets()
{
    auto previous = this->currentTarget();

    this->replyable_ =
        this->mentions_ == nullptr
            ? std::vector<MessagePtr>{}
            : replyableMentions(this->mentions_->getMessageSnapshot(),
                                this->channelIsOpen_);

    // Keep pointing at the same mention across a rebuild. Otherwise a mention
    // arriving while a reply is half typed silently redirects it at the new
    // one, which is how you answer the wrong person.
    this->index_ = 0;
    if (previous != nullptr)
    {
        for (std::size_t i = 0; i < this->replyable_.size(); i++)
        {
            if (this->replyable_[i] == previous)
            {
                this->index_ = i;
                break;
            }
        }
    }

    this->refreshStatus();
}

MessagePtr MentionsPopup::currentTarget() const
{
    if (this->index_ >= this->replyable_.size())
    {
        return {};
    }

    return this->replyable_[this->index_];
}

void MentionsPopup::step(int delta)
{
    if (this->replyable_.empty())
    {
        return;
    }

    // Newest first, so stepping to an older mention moves forward through the
    // list. Clamped rather than wrapped: silently jumping from the oldest back
    // to the newest is a good way to answer the wrong message.
    auto last = static_cast<long long>(this->replyable_.size()) - 1;
    auto next = static_cast<long long>(this->index_) - delta;
    next = std::clamp(next, 0LL, last);

    this->index_ = static_cast<std::size_t>(next);
    this->refreshStatus();
}

QString MentionsPopup::statusText() const
{
    return this->statusFull_;
}

void MentionsPopup::refreshStatus()
{
    if (this->status_ == nullptr || this->input_ == nullptr ||
        this->reply_ == nullptr || this->older_ == nullptr ||
        this->newer_ == nullptr)
    {
        return;
    }

    auto target = this->currentTarget();
    bool canReply = target != nullptr;

    this->input_->setEnabled(canReply);
    this->reply_->setEnabled(canReply);
    this->older_->setEnabled(this->index_ + 1 < this->replyable_.size());
    this->newer_->setEnabled(this->index_ > 0);

    if (canReply)
    {
        this->statusFull_ = describeMention(*target);
        this->status_->setToolTip(target->messageText);
        this->applyStatusElision();
        return;
    }

    this->status_->setToolTip({});

    if (this->mentions_ == nullptr ||
        this->mentions_->getMessageSnapshot().empty())
    {
        this->statusFull_ = tr("Nobody has mentioned you yet.");
        this->applyStatusElision();
        return;
    }

    // There are mentions, but none can be answered. Say why for the newest one
    // rather than leaving the box disabled with no explanation.
    auto all = this->mentions_->getMessageSnapshot();
    for (auto it = all.rbegin(); it != all.rend(); ++it)
    {
        if (*it == nullptr)
        {
            continue;
        }

        auto problem = mentionReplyProblem(
            **it, this->channelIsOpen_ ? this->channelIsOpen_((*it)->channelName)
                                       : false);
        if (!problem.isEmpty())
        {
            this->statusFull_ = problem;
            this->applyStatusElision();
            return;
        }
    }

    this->statusFull_ = tr("None of these can be replied to.");
    this->applyStatusElision();
}

void MentionsPopup::applyStatusElision()
{
    if (this->status_ == nullptr)
    {
        return;
    }

    auto width = this->status_->width();
    if (width <= 0)
    {
        // Before the first layout there is no width to fit anything into.
        this->status_->setText(this->statusFull_);
        return;
    }

    this->status_->setText(this->status_->fontMetrics().elidedText(
        this->statusFull_, Qt::ElideRight, width));
}

void MentionsPopup::resizeEvent(QResizeEvent *event)
{
    BasePopup::resizeEvent(event);
    this->applyStatusElision();
}

void MentionsPopup::send()
{
    if (this->input_ == nullptr)
    {
        return;
    }

    auto text = this->input_->text().trimmed();
    if (text.isEmpty())
    {
        return;
    }

    auto target = this->currentTarget();
    if (target == nullptr)
    {
        return;
    }

    bool isOpen =
        this->channelIsOpen_ ? this->channelIsOpen_(target->channelName) : false;
    auto where = mentionReplyTarget(*target, isOpen);
    if (!where.has_value())
    {
        // It went stale between being offered and being answered — an hour with
        // the window open is enough. Say so rather than sending into nothing.
        this->rebuildTargets();
        return;
    }

    if (this->send_)
    {
        this->send_(target, *where, text);
    }

    this->input_->clear();
}

bool MentionsPopup::sendForTest(const QString &text)
{
    if (this->input_ == nullptr)
    {
        return false;
    }

    this->input_->setText(text);
    this->send();

    return this->input_->text().isEmpty() && !text.trimmed().isEmpty();
}

}  // namespace chatterino
