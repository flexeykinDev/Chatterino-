// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/buttons/InitMentionsButton.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "providers/twitch/Mentions.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "widgets/buttons/LabelButton.hpp"
#include "widgets/dialogs/MentionsPopup.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QPointer>

namespace chatterino {

void initMentionsButton(LabelButton &button,
                        const std::function<void()> &relayout,
                        pajlada::Signals::SignalHolder &signalHolder)
{
    QPointer<LabelButton> safe(&button);

    auto refresh = [safe, relayout] {
        if (safe.isNull())
        {
            return;
        }

        auto label = mentionButtonLabel(MentionsPopup::unreadCount());
        if (safe->text() == label)
        {
            return;
        }

        safe->setText(label);
        if (relayout)
        {
            relayout();
        }
    };

    auto applyVisibility = [safe, relayout](bool hide) {
        if (safe.isNull())
        {
            return;
        }

        bool wasVisible = safe->isVisible();
        safe->setVisible(!hide);
        if (wasVisible == hide && relayout)
        {
            relayout();
        }
    };

    button.setText(mentionButtonLabel(0));
    button.setToolTip(
        QCoreApplication::translate("MentionsButton",
                                    "Everywhere you have been mentioned, with "
                                    "a box to reply from."));

    QObject::connect(&button, &Button::leftClicked, &button, [safe] {
        if (!safe.isNull())
        {
            MentionsPopup::showUnique(safe->window());
        }
    });

    applyVisibility(getSettings()->hideMentionsButton.getValue());
    getSettings()->hideMentionsButton.connect(applyVisibility, signalHolder,
                                              false);

    // The count has to follow both a mention arriving and another window
    // marking them read, which is why what has been read lives in a setting
    // rather than in whichever window noticed first.
    if (auto *app = tryGetApp())
    {
        if (auto *twitch = app->getTwitch())
        {
            signalHolder.managedConnect(twitch->getMentionsChannel()
                                            ->messageAppended,
                                        [refresh](auto &, auto) {
                                            refresh();
                                        });
        }
    }

    getSettings()->lastSeenMention.connect(
        [refresh](const auto &) {
            refresh();
        },
        signalHolder, false);

    refresh();
}

}  // namespace chatterino
