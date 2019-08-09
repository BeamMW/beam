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

using namespace beam::wallet;

QDateTime SwapOfferItem::time() const
{
    beam::Timestamp time;
    if (m_offer.GetParameter(TxParameterID::ModifyTime, time))
    {
        QDateTime datetime;
        datetime.setTime_t(time);
        return datetime;
    }
    else
    {
        return QDateTime();
    }
}

TxID SwapOfferItem::rawId() const
{
    auto id = m_offer.GetTxID();
    
    if (id.has_value())
    {
        return id.value();
    }
    else
    {
        return TxID();
    }
}

QString SwapOfferItem::id() const
{
    TxID txId = rawId();
    return QString::fromStdString(beam::to_hex(txId.data(), txId.size()));
}

beam::Amount SwapOfferItem::rawAmount() const
{
    beam::Amount amount;
    if (m_offer.GetParameter(TxParameterID::Amount, amount))
    {
        return amount;
    }
    return 0;
}

beam::Amount SwapOfferItem::rawAmountSwap() const
{
    beam::Amount amount;
    if (m_offer.GetParameter(TxParameterID::AtomicSwapAmount, amount))
    {
        return amount;
    }
    return 0;
}

QString SwapOfferItem::amount() const
{
    return beamui::BeamToString(rawAmount());
}

QString SwapOfferItem::amountSwap() const
{
    return beamui::BeamToString(rawAmountSwap());
}

QString SwapOfferItem::message() const
{
    beam::ByteBuffer message;
    if (m_offer.GetParameter(TxParameterID::Message, message))
    {
        std::string s(message.begin(), message.end());
        return QString::fromStdString(s);
    }
    return QString();
}

beam::wallet::TxParameters SwapOfferItem::getTxParameters() const
{
    return m_offer;
}
