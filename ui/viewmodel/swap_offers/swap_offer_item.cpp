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
#include "wallet/common.h"

SwapOfferItem::SwapOfferItem(const SwapOffer& offer, bool isOwn)
    : m_offer{offer},
      m_isOwnOffer{isOwn}
{
    m_offer.GetParameter(TxParameterID::AtomicSwapIsBeamSide, m_isBeamSide);
}

auto SwapOfferItem::timeCreated() const -> QDateTime
{
    beam::Timestamp time;
    if (m_offer.GetParameter(TxParameterID::CreateTime, time))
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

auto SwapOfferItem::timeExpiration() const -> QDateTime
{
    beam::Timestamp time;
    if (m_offer.GetParameter(TxParameterID::CreateTime, time))
    {
        QDateTime datetime;
        time += 60*60*24;       // defaut lifetime 24h
        datetime.setTime_t(time);
        // TODO : minus lifetime
        // if (m_offer.GetParameter(beam::wallet::TxParameterID::PeerResponseHeight, x))
        return datetime;
    }
    else
    {
        return QDateTime();
    }
}

auto SwapOfferItem::rawAmountSend() const -> beam::Amount
{
    beam::wallet::TxParameterID paramID = m_isBeamSide ? TxParameterID::AtomicSwapAmount : TxParameterID::Amount;
    beam::Amount amount;
    if (m_offer.GetParameter(paramID, amount))
    {
        return amount;
    }
    return 0;
}

auto SwapOfferItem::rawAmountReceive() const -> beam::Amount
{
    beam::wallet::TxParameterID paramID = m_isBeamSide ? TxParameterID::Amount : TxParameterID::AtomicSwapAmount;
    beam::Amount amount;
    if (m_offer.GetParameter(paramID, amount))
    {
        return amount;
    }
    return 0;
}

auto SwapOfferItem::rate() const -> double
{
    double amountReceive = double(int64_t(rawAmountReceive()));
    double amountSend = double(int64_t(rawAmountSend()));
    double rate = amountReceive / amountSend;

    double p = pow( 10., 7 );
    return floor( rate * p + .5 ) / p;
}

auto SwapOfferItem::amountSend() const -> QString
{
    beamui::Currencies coinType = m_isBeamSide ? getSwapCoinType() : beamui::Currencies::Beam;

    return beamui::AmountToString(rawAmountSend(), coinType);
}

auto SwapOfferItem::amountReceive() const -> QString
{
    beamui::Currencies coinType = m_isBeamSide ? beamui::Currencies::Beam : getSwapCoinType();
    
    return beamui::AmountToString(rawAmountReceive(), coinType);
}

auto SwapOfferItem::isOwnOffer() const -> bool
{
    return m_isOwnOffer;
}

auto SwapOfferItem::getTxParameters() const -> beam::wallet::TxParameters
{
    return m_offer;
}

auto SwapOfferItem::getSwapCoinType() const -> beamui::Currencies
{
    beam::wallet::AtomicSwapCoin coin;
    if (m_offer.GetParameter(TxParameterID::AtomicSwapCoin, coin))
    {
        switch (coin)
        {
            case AtomicSwapCoin::Bitcoin:   return beamui::Currencies::Bitcoin;
            case AtomicSwapCoin::Litecoin:  return beamui::Currencies::Litecoin;
            case AtomicSwapCoin::Qtum:      return beamui::Currencies::Qtum;
            case AtomicSwapCoin::Unknown:   return beamui::Currencies::Unknown;
        }
    }
    return beamui::Currencies::Unknown;
}
