// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/companion/PresenceRegistry.hpp"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <optional>

namespace chatterino {

struct GlobalBanSummary;

/// Builds the companion service's ban-registry URLs.
///
/// Kept apart from the requests themselves so that the fiddly part — joining
/// ids, escaping a channel, not mangling a base URL that ends in a slash — can
/// be tested without a server to talk to.
class CompanionUrls
{
public:
    /// `baseUrl` is what the setting holds, with or without a trailing slash.
    /// An empty base makes every builder return nothing, which is how "the
    /// service is not configured" travels through the rest of the code.
    explicit CompanionUrls(QString baseUrl);

    [[nodiscard]] bool isConfigured() const;

    /// `GET /v1/bans?ids=&channel=` — marker counts for many chatters at once.
    [[nodiscard]] std::optional<QUrl> markers(const QStringList &userIds,
                                              const QString &channelId) const;

    /// `GET /v1/bans/:offenderId?channel=` — one chatter's whole history.
    [[nodiscard]] std::optional<QUrl> history(const QString &offenderId,
                                              const QString &channelId) const;

    /// `GET /v1/presence?ids=` — who is running this client.
    [[nodiscard]] std::optional<QUrl> presence(
        const QStringList &userIds) const;

    /// `POST`/`DELETE /v1/bans/:offenderId/vouch` — vouch for someone, or take
    /// the vouch back. Both verbs share a URL; the caller picks the verb.
    [[nodiscard]] std::optional<QUrl> vouch(const QString &offenderId) const;

    /// The realtime socket, on the same origin as the API with the scheme
    /// swapped: `http` becomes `ws`, `https` becomes `wss`. Getting that pair
    /// wrong fails at connect time with an error about the scheme rather than
    /// about the address, which reads like a misconfigured server.
    [[nodiscard]] std::optional<QUrl> socket() const;

private:
    /// Normalised: no trailing slash, empty when unconfigured.
    QString baseUrl_;
};

/// Talks to the companion service's ban registry.
///
/// Every call is a no-op when the service is not configured or the user is not
/// signed in, because the service authenticates with the caller's own Twitch
/// token. Failures are reported to the callback rather than retried here; the
/// caller knows whether a retry is worth it.
class CompanionApi
{
public:
    /// Called with the answered ids and their marker counts, or with a failure.
    using MarkersCallback =
        std::function<void(std::optional<QHash<QString, int>>)>;
    using SummaryCallback = std::function<void(std::optional<GlobalBanSummary>)>;
    using PresenceCallback =
        std::function<void(std::optional<QHash<QString, PresenceState>>)>;
    using ChangedCallback = std::function<void(bool ok)>;

    CompanionApi() = default;

    void setBaseUrl(const QString &baseUrl);
    [[nodiscard]] bool isConfigured() const;

    void fetchMarkers(const QStringList &userIds, const QString &channelId,
                      MarkersCallback callback) const;
    void fetchHistory(const QString &offenderId, const QString &channelId,
                      SummaryCallback callback) const;
    void fetchPresence(const QStringList &userIds,
                       PresenceCallback callback) const;

    /// Vouches for a chatter on `channelId`: that chat has decided it trusts
    /// them, so no marker is shown there. It changes nothing for any other
    /// channel. Only a moderator of that channel may do this; the service
    /// enforces that, we do not guess at it.
    void vouch(const QString &offenderId, const QString &channelId,
               ChangedCallback callback) const;
    void withdrawVouch(const QString &offenderId, const QString &channelId,
                       ChangedCallback callback) const;

private:
    /// The signed-in user's Twitch token, or nothing when signed out.
    [[nodiscard]] static std::optional<QString> token();

    CompanionUrls urls_{QString{}};
};

}  // namespace chatterino
