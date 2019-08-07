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

QString SwapOfferItem::id() const
{
    auto id = m_offer.GetTxID();
    
    if (id.has_value())
    {
        value = id.value();
        beam::to_hex(value.data(), value.size());
        return QString::fromStdString(value); 
    }
    else
    {
        return QString("undefined");
    }
}

beam::Amount SwapOfferItem::rawAmount() const
{
    beam::Amount amount;
    if (m_offer.GetParameter(TxParameterID::Amount, amount))
    {
        return amount;
    }
    else
    {
        return 0;
    }
}

QString SwapOfferItem::amount() const
{
    return beamui::BeamToString(rawAmount());
}

QString SwapOfferItem::status() const
{
    beam::wallet::TxStatus status;
    if (m_offer.GetParameter(TxParameterID::Status, status))
    {
        // todo - replace with status toString
        beam::wallet::TxDescription desc;
        desc.m_status = status;
        return desc.getStatusString().c_str();
    }
    else
    {
        return QString("undefined");
    }
}

QString SwapOfferItem::message() const
{
    beam::ByteBuffer message;
    if (m_offer.GetParameter(TxParameterID::Message, message))
    {
        std::string string;
        if (beam::wallet::fromByteBuffer(message, string))
        {
            return QString::fromStdString(string);
        }
    }
    return QString();
}
