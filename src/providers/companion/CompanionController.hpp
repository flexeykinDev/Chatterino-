// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/companion/CompanionApi.hpp"
#include "providers/companion/GlobalBanRegistry.hpp"
#include "singletons/Settings.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QObject>
#include <QTimer>

#include <functional>

namespace chatterino {

struct GlobalBanSummary;

/// Owns the client's half of the cross-channel ban registry.
///
/// Message building calls {@link noteChatter} for every chatter it lays out,
/// which is far more often than a request should be made, so questions are
/// collected and asked in one batch shortly after. The delay is what turns a
/// busy chat's hundreds of messages a minute into a handful of requests.
///
/// Nothing here runs when the service address setting is empty, which is its
/// default: asking a third-party server who is in a chat is not something to do
/// without being told to.
class CompanionController : public QObject
{
    Q_OBJECT

public:
    CompanionController();

    /// Adopts the address setting and keeps following it, so changing it in
    /// the settings dialog takes effect at once rather than on restart.
    void followSetting(QStringSetting &setting);

    /// Points the controller at a service, or at none when `baseUrl` is empty.
    /// Everything already learned is dropped, since a different service is a
    /// different set of answers.
    void setBaseUrl(const QString &baseUrl);

    [[nodiscard]] bool isEnabled() const;

    [[nodiscard]] GlobalBanRegistry &globalBans();
    [[nodiscard]] const GlobalBanRegistry &globalBans() const;

    /// Notes that a chatter appeared in a channel, so the next batch asks about
    /// them. Cheap and safe to call from message building; it does no I/O.
    void noteChatter(const QString &channelId, const QString &userId);

    /// Fetches one chatter's full history. The callback runs on the GUI thread
    /// and receives nothing when the service is unreachable or unconfigured.
    void fetchHistory(const QString &offenderId, const QString &channelId,
                      const std::function<void(std::optional<GlobalBanSummary>)>
                          &callback);

    /// Vouches for a chatter on a channel, then re-asks for their marker so the
    /// change shows without waiting for them to speak again.
    void vouch(const QString &offenderId, const QString &channelId,
               const std::function<void(bool)> &callback);
    void withdrawVouch(const QString &offenderId, const QString &channelId,
                       const std::function<void(bool)> &callback);

    /// Sends whatever is queued right now, ignoring the batching delay.
    void flush();

private:
    void scheduleFlush();
    /// Relayouts every channel view, because a marker appearing changes the
    /// width of the messages that carry it.
    static void relayout();

    CompanionApi api_;
    GlobalBanRegistry registry_;

    /// Long enough that a burst of messages becomes one request, short enough
    /// that a marker appears while the message is still on screen.
    QTimer flushTimer_;
    /// How many batches are in flight, so a slow service cannot be asked the
    /// same question by every message that arrives meanwhile.
    int inFlight_ = 0;

    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace chatterino
