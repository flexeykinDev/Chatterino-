// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/ShadowBacklog.hpp"

namespace chatterino {

bool ShadowBacklog::isNew(qint64 id) const
{
    if (id <= 0)
    {
        return true;
    }

    return id > this->highestSeen_;
}

void ShadowBacklog::seen(qint64 id)
{
    if (id > this->highestSeen_)
    {
        this->highestSeen_ = id;
    }
}

qint64 ShadowBacklog::highestSeen() const
{
    return this->highestSeen_;
}

}  // namespace chatterino
