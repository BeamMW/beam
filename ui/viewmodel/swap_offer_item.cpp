// Copyright 2019 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "swap_offer_item.h"
#include "utility/helpers.h"
#include "ui_helpers.h"
#include "wallet/common.h"

QDateTime SwapOfferItem::time() const
{
    QDateTime datetime;
    datetime.setTime_t(m_offer.m_modifyTime);
    return datetime;
}

QString SwapOfferItem::id() const
{
    auto id = beam::to_hex(m_offer.m_txId.data(), m_offer.m_txId.size());
    return QString::fromStdString(id);
}

QString SwapOfferItem::amount() const
{
    return beamui::BeamToString(m_offer.m_amount);
}

QString SwapOfferItem::status() const
{
    return m_offer.getStatusString().c_str();
}

beam::Amount SwapOfferItem::rawAmount() const
{
    return m_offer.m_amount;
}

QString SwapOfferItem::message() const
{
    std::string msg;
    if(beam::wallet::fromByteBuffer(m_offer.m_message, msg))
        return QString::fromStdString(msg);
    else
        return QString();
}
