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
#include "ui/viewmodel/ui_helpers.h"

SwapOfferItem::SwapOfferItem(const SwapOffer& offer, bool isOwn, const QDateTime& timeExpiration)
    : m_offer{offer}
    , m_isOwnOffer{isOwn}
    , m_timeExpiration{timeExpiration}
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
    return m_timeExpiration;
}

auto SwapOfferItem::rawAmountSend() const -> beam::Amount
{
    auto paramID = isBeamSide()
        ? TxParameterID::AtomicSwapAmount
        : TxParameterID::Amount;
    beam::Amount amount;
    if (m_offer.GetParameter(paramID, amount))
    {
        return amount;
    }
    return 0;
}

auto SwapOfferItem::rawAmountReceive() const -> beam::Amount
{
    auto paramID = isBeamSide()
        ? TxParameterID::Amount
        : TxParameterID::AtomicSwapAmount;
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
    auto coinType = isBeamSide()
        ? getSwapCoinType()
        : beamui::Currencies::Beam;

    return beamui::AmountToString(rawAmountSend(), coinType);
}

auto SwapOfferItem::amountReceive() const -> QString
{
    auto coinType = isBeamSide()
        ? beamui::Currencies::Beam
        : getSwapCoinType();
    
    return beamui::AmountToString(rawAmountReceive(), coinType);
}

auto SwapOfferItem::isOwnOffer() const -> bool
{
    return m_isOwnOffer;
}

auto SwapOfferItem::isBeamSide() const -> bool
{
    return m_isOwnOffer ? m_isBeamSide : !m_isBeamSide;
}

auto SwapOfferItem::getTxParameters() const -> beam::wallet::TxParameters
{
    return m_offer;
}

auto SwapOfferItem::getTxID() const -> TxID
{
    return m_offer.m_txId;    
}

auto SwapOfferItem::getSwapCoinType() const -> beamui::Currencies
{
    beam::wallet::AtomicSwapCoin coin;
    if (m_offer.GetParameter(TxParameterID::AtomicSwapCoin, coin))
    {
        return beamui::convertSwapCoinToCurrency(coin);
    }
    return beamui::Currencies::Unknown;
}

auto SwapOfferItem::getSwapCoinName() const -> QString
{
    switch (getSwapCoinType())
    {
        case beamui::Currencies::Bitcoin:   return toString(beamui::Currencies::Bitcoin);
        case beamui::Currencies::Litecoin:  return toString(beamui::Currencies::Litecoin);
        case beamui::Currencies::Qtum:      return toString(beamui::Currencies::Qtum);
        default:                            return toString(beamui::Currencies::Unknown);
    }
}
