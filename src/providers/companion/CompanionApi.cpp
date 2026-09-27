// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/CompanionApi.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/companion/GlobalBan.hpp"
#include "providers/twitch/TwitchAccount.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#include <utility>

namespace {

/// The service's requests are small and a slow one should not pile up behind
/// the next batch, so this is deliberately shorter than the emote fetches.
constexpr int requestTimeoutMs = 10000;

}  // namespace

namespace chatterino {

CompanionUrls::CompanionUrls(QString baseUrl)
{
    auto trimmed = baseUrl.trimmed();
    while (trimmed.endsWith('/'))
    {
        trimmed.chop(1);
    }

    // A base that is not a usable absolute URL is treated as unconfigured
    // rather than half-used: a relative request would otherwise be sent to
    // whatever host the URL happened to resolve against.
    QUrl parsed(trimmed);
    if (!parsed.isValid() || parsed.scheme().isEmpty() ||
        parsed.host().isEmpty())
    {
        return;
    }

    this->baseUrl_ = trimmed;
}

bool CompanionUrls::isConfigured() const
{
    return !this->baseUrl_.isEmpty();
}

std::optional<QUrl> CompanionUrls::markers(const QStringList &userIds,
                                           const QString &channelId) const
{
    if (!this->isConfigured() || userIds.isEmpty())
    {
        return std::nullopt;
    }

    QUrl url(this->baseUrl_ + "/v1/bans");

    QUrlQuery query;
    query.addQueryItem("ids", userIds.join(','));
    query.addQueryItem("channel", channelId);
    url.setQuery(query);

    return url;
}

std::optional<QUrl> CompanionUrls::history(const QString &offenderId,
                                           const QString &channelId) const
{
    if (!this->isConfigured() || offenderId.isEmpty())
    {
        return std::nullopt;
    }

    QUrl url(this->baseUrl_ + "/v1/bans/" +
             QString::fromUtf8(QUrl::toPercentEncoding(offenderId)));

    QUrlQuery query;
    query.addQueryItem("channel", channelId);
    url.setQuery(query);

    return url;
}

std::optional<QUrl> CompanionUrls::vouch(const QString &offenderId) const
{
    if (!this->isConfigured() || offenderId.isEmpty())
    {
        return std::nullopt;
    }

    return QUrl(this->baseUrl_ + "/v1/bans/" +
                QString::fromUtf8(QUrl::toPercentEncoding(offenderId)) +
                "/vouch");
}

void CompanionApi::setBaseUrl(const QString &baseUrl)
{
    this->urls_ = CompanionUrls(baseUrl);
}

bool CompanionApi::isConfigured() const
{
    return this->urls_.isConfigured();
}

std::optional<QString> CompanionApi::token()
{
    // The account controller is absent in an application built without one,
    // where reaching through it is an access violation rather than a failed
    // assertion once asserts are compiled out.
    auto *accounts = getApp()->getAccounts();
    if (accounts == nullptr)
    {
        return std::nullopt;
    }

    auto account = accounts->twitch.getCurrent();
    if (!account || account->isAnon())
    {
        return std::nullopt;
    }

    auto token = account->getOAuthToken();
    if (token.isEmpty())
    {
        return std::nullopt;
    }

    return token;
}

void CompanionApi::fetchMarkers(const QStringList &userIds,
                                const QString &channelId,
                                MarkersCallback callback) const
{
    auto url = this->urls_.markers(userIds, channelId);
    auto token = CompanionApi::token();
    if (!url || !token)
    {
        callback(std::nullopt);
        return;
    }

    NetworkRequest(*url)
        .timeout(requestTimeoutMs)
        .header("Authorization", "Bearer " + *token)
        .onSuccess([callback](const NetworkResult &result) {
            callback(parseGlobalBanMarkers(result.parseJson()));
        })
        .onError([callback](const NetworkResult &result) {
            qCWarning(chatterinoApp)
                << "Companion service refused a ban marker batch:"
                << result.formatError();
            callback(std::nullopt);
        })
        .execute();
}

void CompanionApi::fetchHistory(const QString &offenderId,
                                const QString &channelId,
                                SummaryCallback callback) const
{
    auto url = this->urls_.history(offenderId, channelId);
    auto token = CompanionApi::token();
    if (!url || !token)
    {
        callback(std::nullopt);
        return;
    }

    NetworkRequest(*url)
        .timeout(requestTimeoutMs)
        .header("Authorization", "Bearer " + *token)
        .onSuccess([callback](const NetworkResult &result) {
            callback(GlobalBanSummary::fromJson(result.parseJson()));
        })
        .onError([callback](const NetworkResult &result) {
            qCWarning(chatterinoApp)
                << "Companion service refused a ban history:"
                << result.formatError();
            callback(std::nullopt);
        })
        .execute();
}

void CompanionApi::vouch(const QString &offenderId, const QString &channelId,
                         ChangedCallback callback) const
{
    auto url = this->urls_.vouch(offenderId);
    auto token = CompanionApi::token();
    if (!url || !token)
    {
        callback(false);
        return;
    }

    QJsonObject body;
    body.insert("channelId", channelId);

    NetworkRequest(*url, NetworkRequestType::Post)
        .timeout(requestTimeoutMs)
        .header("Authorization", "Bearer " + *token)
        .header("Content-Type", "application/json")
        .payload(QJsonDocument(body).toJson(QJsonDocument::Compact))
        .onSuccess([callback](const NetworkResult &) {
            callback(true);
        })
        .onError([callback](const NetworkResult &result) {
            qCWarning(chatterinoApp)
                << "Companion service refused a vouch:" << result.formatError();
            callback(false);
        })
        .execute();
}

void CompanionApi::withdrawVouch(const QString &offenderId,
                                 const QString &channelId,
                                 ChangedCallback callback) const
{
    auto url = this->urls_.vouch(offenderId);
    auto token = CompanionApi::token();
    if (!url || !token)
    {
        callback(false);
        return;
    }

    QJsonObject body;
    body.insert("channelId", channelId);

    NetworkRequest(*url, NetworkRequestType::Delete)
        .timeout(requestTimeoutMs)
        .header("Authorization", "Bearer " + *token)
        .header("Content-Type", "application/json")
        .payload(QJsonDocument(body).toJson(QJsonDocument::Compact))
        .onSuccess([callback](const NetworkResult &) {
            callback(true);
        })
        .onError([callback](const NetworkResult &result) {
            qCWarning(chatterinoApp)
                << "Companion service refused a vouch withdrawal:"
                << result.formatError();
            callback(false);
        })
        .execute();
}

}  // namespace chatterino
