// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/CompanionHealth.hpp"

#include <QCoreApplication>

namespace chatterino {

bool CompanionHealth::setAvailability(bool configured, bool signedIn)
{
    auto before = this->status();

    // A different address, or a different account, makes everything learned so
    // far say nothing about whether the service is answering now.
    if (configured != this->configured_ || signedIn != this->signedIn_)
    {
        this->consecutiveFailures_ = 0;
        this->everSucceeded_ = false;
    }

    this->configured_ = configured;
    this->signedIn_ = signedIn;

    return this->status() != before;
}

bool CompanionHealth::recordSuccess()
{
    auto before = this->status();

    this->consecutiveFailures_ = 0;
    this->everSucceeded_ = true;

    return this->status() != before;
}

bool CompanionHealth::recordFailure()
{
    auto before = this->status();

    // Saturating, so a service that is down for an hour does not overflow its
    // way back to looking healthy.
    if (this->consecutiveFailures_ < failuresBeforeUnreachable)
    {
        this->consecutiveFailures_++;
    }

    return this->status() != before;
}

CompanionStatus CompanionHealth::status() const
{
    return this->compute();
}

CompanionStatus CompanionHealth::compute() const
{
    if (!this->configured_)
    {
        return CompanionStatus::Disabled;
    }

    if (!this->signedIn_)
    {
        return CompanionStatus::SignedOut;
    }

    if (this->consecutiveFailures_ >= failuresBeforeUnreachable)
    {
        return CompanionStatus::Unreachable;
    }

    if (this->everSucceeded_)
    {
        return CompanionStatus::Reachable;
    }

    return CompanionStatus::Unknown;
}

QString describeCompanionStatus(CompanionStatus status)
{
    switch (status)
    {
        case CompanionStatus::Disabled:
            return QCoreApplication::translate(
                "CompanionHealth",
                "Off. Nothing is contacted until an address is set.");

        case CompanionStatus::SignedOut:
            return QCoreApplication::translate(
                "CompanionHealth",
                "Waiting for a Twitch login. The service identifies you by "
                "your own token, so it can answer nothing while you are "
                "signed out.");

        case CompanionStatus::Unknown:
            return QCoreApplication::translate("CompanionHealth",
                                               "Configured. Nothing asked yet.");

        case CompanionStatus::Reachable:
            return QCoreApplication::translate("CompanionHealth",
                                               "Answering.");

        case CompanionStatus::Unreachable:
            return QCoreApplication::translate(
                "CompanionHealth",
                "Not answering. Companion features show nothing at all while "
                "this is the case, which looks the same as having nothing to "
                "show.");
    }

    return {};
}

}  // namespace chatterino
