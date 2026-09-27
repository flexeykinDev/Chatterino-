// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/CreatePollDialog.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int dialogWidth = 420;

}  // namespace

namespace chatterino {

QStringList PollDraft::filledChoices() const
{
    QStringList filled;
    for (const auto &choice : this->choices)
    {
        auto trimmed = choice.trimmed();
        if (!trimmed.isEmpty())
        {
            filled.append(trimmed);
        }
    }

    return filled;
}

QString PollDraft::problem() const
{
    auto title = this->title.trimmed();
    if (title.isEmpty())
    {
        return QCoreApplication::translate("PollDraft", "Give the poll a question.");
    }

    if (title.length() > maxTitleLength)
    {
        return QCoreApplication::translate(
                   "PollDraft", "The question is %1 characters; Twitch allows %2.")
            .arg(title.length())
            .arg(maxTitleLength);
    }

    auto filled = this->filledChoices();
    if (filled.size() < minChoices)
    {
        return QCoreApplication::translate("PollDraft",
                                           "A poll needs at least two choices.");
    }

    if (filled.size() > maxChoices)
    {
        return QCoreApplication::translate(
                   "PollDraft", "Twitch allows at most %1 choices.")
            .arg(maxChoices);
    }

    for (const auto &choice : filled)
    {
        if (choice.length() > maxChoiceLength)
        {
            return QCoreApplication::translate(
                       "PollDraft",
                       "\"%1\" is too long; a choice may be %2 characters.")
                .arg(choice)
                .arg(maxChoiceLength);
        }
    }

    auto seconds = this->duration.count();
    if (seconds < minDurationSeconds)
    {
        return QCoreApplication::translate(
                   "PollDraft", "A poll must run for at least %1 seconds.")
            .arg(minDurationSeconds);
    }

    if (seconds > maxDurationSeconds)
    {
        return QCoreApplication::translate(
                   "PollDraft", "A poll may run for at most %1 minutes.")
            .arg(maxDurationSeconds / 60);
    }

    return {};
}

CreatePollDialog::CreatePollDialog(ChannelPtr channel, QWidget *parent)
    : BasePopup({BaseWindow::EnableCustomFrame, BaseWindow::Dialog}, parent)
    , channel_(std::move(channel))
{
    this->setWindowTitle(tr("Create a poll"));
    this->setMinimumWidth(dialogWidth);

    auto *root = new QVBoxLayout(this->getLayoutContainer());

    auto *form = new QFormLayout();
    root->addLayout(form);

    this->title_ = new QLineEdit(this);
    this->title_->setMaxLength(PollDraft::maxTitleLength);
    this->title_->setPlaceholderText(tr("What are we voting on?"));
    form->addRow(tr("Question"), this->title_);

    // Minutes and seconds separately, because a duration in seconds alone
    // means doing arithmetic to ask for two minutes.
    auto *durationRow = new QHBoxLayout();
    this->durationMinutes_ = new QSpinBox(this);
    this->durationMinutes_->setRange(0, PollDraft::maxDurationSeconds / 60);
    this->durationMinutes_->setValue(2);
    this->durationMinutes_->setSuffix(tr(" min"));
    this->durationSeconds_ = new QSpinBox(this);
    this->durationSeconds_->setRange(0, 59);
    this->durationSeconds_->setSuffix(tr(" sec"));
    durationRow->addWidget(this->durationMinutes_);
    durationRow->addWidget(this->durationSeconds_);
    durationRow->addStretch(1);
    form->addRow(tr("Runs for"), durationRow);

    this->points_ = new QSpinBox(this);
    this->points_->setRange(0, 1000000);
    this->points_->setSingleStep(100);
    this->points_->setSpecialValueText(tr("off"));
    this->points_->setToolTip(
        tr("Lets viewers spend channel points on extra votes. Zero turns it "
           "off."));
    form->addRow(tr("Points per extra vote"), this->points_);

    root->addWidget(new QLabel(tr("Choices"), this));
    this->choiceRows_ = new QVBoxLayout();
    root->addLayout(this->choiceRows_);

    this->addChoice_ = new QPushButton(tr("Add a choice"), this);
    QObject::connect(this->addChoice_, &QPushButton::clicked, this, [this] {
        this->addChoiceRow();
    });
    root->addWidget(this->addChoice_);

    this->problem_ = new QLabel(this);
    this->problem_->setWordWrap(true);
    root->addWidget(this->problem_);

    auto *buttons = new QDialogButtonBox(this);
    this->create_ = buttons->addButton(tr("Create poll"),
                                       QDialogButtonBox::AcceptRole);
    // Our own text rather than QDialogButtonBox::Cancel. The standard button
    // takes its wording from Qt's catalogue, which a deployed build may not
    // have, and a lone English word on an otherwise translated dialog is the
    // kind of thing that only ever gets noticed by the person using it.
    auto *cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    QObject::connect(this->create_, &QPushButton::clicked, this, [this] {
        this->submit();
    });
    QObject::connect(cancel, &QPushButton::clicked, this, [this] {
        this->close();
    });
    root->addWidget(buttons);

    QObject::connect(this->title_, &QLineEdit::textChanged, this, [this] {
        this->refreshValidity();
    });
    QObject::connect(this->durationMinutes_, &QSpinBox::valueChanged, this,
                     [this] {
                         this->refreshValidity();
                     });
    QObject::connect(this->durationSeconds_, &QSpinBox::valueChanged, this,
                     [this] {
                         this->refreshValidity();
                     });

    // Only now that everything refreshValidity() touches exists. Adding a row
    // refreshes, and doing it earlier dereferenced widgets not yet built.
    //
    // Two rows to begin with: the fewest a poll can have, so the form opens
    // showing exactly what must be filled in.
    this->addChoiceRow();
    this->addChoiceRow();

    this->refreshValidity();
    this->title_->setFocus();
}

void CreatePollDialog::addChoiceForTest(const QString &text)
{
    this->addChoiceRow(text);
}

void CreatePollDialog::addChoiceRow(const QString &text)
{
    if (this->choices_.size() >= PollDraft::maxChoices)
    {
        return;
    }

    auto *row = new QHBoxLayout();
    auto *edit = new QLineEdit(text, this);
    edit->setMaxLength(PollDraft::maxChoiceLength);
    edit->setPlaceholderText(
        tr("Choice %1").arg(this->choices_.size() + 1));

    auto *remove = new QPushButton(QStringLiteral("×"), this);
    remove->setFixedWidth(28);
    remove->setToolTip(tr("Remove this choice"));

    row->addWidget(edit);
    row->addWidget(remove);
    this->choiceRows_->addLayout(row);
    this->choices_.push_back(edit);

    QObject::connect(edit, &QLineEdit::textChanged, this, [this] {
        this->refreshValidity();
    });
    QObject::connect(remove, &QPushButton::clicked, this, [this, edit] {
        this->removeChoiceRow(edit);
    });

    this->refreshValidity();
}

void CreatePollDialog::removeChoiceRow(QLineEdit *edit)
{
    // Never below two: a poll with one choice is not a poll, and letting the
    // form reach that state only to refuse to send is worse than not allowing
    // it.
    if (this->choices_.size() <= PollDraft::minChoices)
    {
        edit->clear();
        return;
    }

    auto found = std::ranges::find(this->choices_, edit);
    if (found == this->choices_.end())
    {
        return;
    }

    auto index = std::distance(this->choices_.begin(), found);
    this->choices_.erase(found);

    auto *item = this->choiceRows_->takeAt(static_cast<int>(index));
    if (item != nullptr)
    {
        while (auto *child = item->layout()->takeAt(0))
        {
            if (child->widget() != nullptr)
            {
                child->widget()->deleteLater();
            }
            delete child;
        }
        delete item;
    }

    this->refreshValidity();
}

PollDraft CreatePollDialog::draft() const
{
    PollDraft draft;
    draft.title = this->title_->text();
    draft.pointsPerVote = this->points_->value();
    draft.duration = std::chrono::seconds(
        this->durationMinutes_->value() * 60 + this->durationSeconds_->value());

    for (const auto *choice : this->choices_)
    {
        draft.choices.append(choice->text());
    }

    return draft;
}

void CreatePollDialog::refreshValidity()
{
    // Called while the form is still being built, from anything that adds a
    // row, so it must tolerate a half-made dialog rather than assume one.
    if (this->problem_ == nullptr || this->create_ == nullptr ||
        this->addChoice_ == nullptr)
    {
        return;
    }

    auto problem = this->draft().problem();

    this->problem_->setText(problem);
    this->create_->setEnabled(problem.isEmpty());
    this->addChoice_->setEnabled(this->choices_.size() <
                                 PollDraft::maxChoices);
}

void CreatePollDialog::submit()
{
    auto draft = this->draft();
    if (!draft.problem().isEmpty())
    {
        return;
    }

    auto *twitch = dynamic_cast<TwitchChannel *>(this->channel_.get());
    if (twitch == nullptr || twitch->roomId().isEmpty())
    {
        this->problem_->setText(tr("This is not a Twitch channel."));
        return;
    }

    auto channel = this->channel_;
    auto title = draft.title.trimmed();

    getHelix()->createPoll(
        twitch->roomId(), title, draft.filledChoices(), draft.duration,
        draft.pointsPerVote,
        [channel, title] {
            channel->addSystemMessage(
                QCoreApplication::translate("CreatePollDialog",
                                            "Created poll: '%1'")
                    .arg(title));
        },
        [channel](const auto &error) {
            channel->addSystemMessage(
                QCoreApplication::translate("CreatePollDialog",
                                            "Failed to create poll - %1")
                    .arg(error));
        });

    this->close();
}

}  // namespace chatterino
