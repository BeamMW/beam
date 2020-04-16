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
#include "wallet/core/common.h"
#include "viewmodel/ui_helpers.h"
#include "viewmodel/qml_globals.h"

using namespace beam::wallet;

SwapOfferItem::SwapOfferItem(const SwapOffer& offer,
                             bool isOwn,
                             const QDateTime& timeExpiration)
    : m_offer{offer}
    , m_isOwnOffer{isOwn}
    , m_isBeamSide{offer.isBeamSide()}
    , m_timeExpiration{timeExpiration} {}

bool SwapOfferItem::operator==(const SwapOfferItem& other) const
{
    return getTxID() == other.getTxID();
}

auto SwapOfferItem::timeCreated() const -> QDateTime
{
    QDateTime datetime;
    datetime.setTime_t(m_offer.timeCreated());
    return datetime;
}

auto SwapOfferItem::timeExpiration() const -> QDateTime
{
    return m_timeExpiration;
}

auto SwapOfferItem::rawAmountSend() const -> beam::Amount
{
    return isSendBeam() ? m_offer.amountBeam() : m_offer.amountSwapCoin(); 
}

auto SwapOfferItem::rawAmountReceive() const -> beam::Amount
{
    return isSendBeam() ? m_offer.amountSwapCoin() : m_offer.amountBeam(); 
}

auto SwapOfferItem::rate() const -> QString
{
    beam::Amount otherCoinAmount =
        isSendBeam() ? rawAmountReceive() : rawAmountSend();
    beam::Amount beamAmount =
        isSendBeam() ? rawAmountSend() : rawAmountReceive();

    if (!beamAmount) return QString();

    return QMLGlobals::divideWithPrecision8(beamui::AmountToUIString(otherCoinAmount), beamui::AmountToUIString(beamAmount));
}

auto SwapOfferItem::amountSend() const -> QString
{
    auto coinType = isSendBeam() ? beamui::Currencies::Beam : getSwapCoinType();
    return beamui::AmountToUIString(rawAmountSend(), coinType);
}

auto SwapOfferItem::amountReceive() const -> QString
{
    auto coinType = isSendBeam() ? getSwapCoinType() : beamui::Currencies::Beam;
    return beamui::AmountToUIString(rawAmountReceive(), coinType);
}

auto SwapOfferItem::isOwnOffer() const -> bool
{
    return m_isOwnOffer;
}

auto SwapOfferItem::isSendBeam() const -> bool
{
    return m_isOwnOffer ? !m_isBeamSide : m_isBeamSide;
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
    return beamui::convertSwapCoinToCurrency(m_offer.swapCoinType());
}

auto SwapOfferItem::getSwapCoinName() const -> QString
{
    return toString(getSwapCoinType());
}
