// Copyright 2018 The Beam Team
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

#include "utxo_item.h"
#include "viewmodel/ui_helpers.h"
#include "wallet/core/common.h"

using namespace beam;
using namespace beam::wallet;
using namespace std;
using namespace beamui;

namespace
{
    template<typename T>
    bool compareUtxo(const T& lf, const T& rt, Qt::SortOrder sortOrder)
    {
        if (sortOrder == Qt::DescendingOrder)
            return lf > rt;
        return lf < rt;
    }
}

UtxoItem::UtxoItem(const beam::wallet::Coin& coin)
    : _coin{ coin }
{

}

bool UtxoItem::operator==(const UtxoItem& other) const
{
    return get_ID() == other.get_ID();
}

QString UtxoItem::getAmountWithCurrency() const
{
    return AmountToUIString(rawAmount(), Currencies::Beam);
}

QString UtxoItem::getAmount() const
{
    return AmountToUIString(rawAmount());
}

QString UtxoItem::maturity() const
{
    if (!_coin.IsMaturityValid())
        return QString{ "-" };
    return QString::number(_coin.m_maturity);
}

UtxoViewStatus::EnStatus UtxoItem::status() const
{
    switch (_coin.m_status)
    {
    case Coin::Available:
        return UtxoViewStatus::Available;
    case Coin::Maturing:
        return UtxoViewStatus::Maturing;
    case Coin::Unavailable:
        return UtxoViewStatus::Unavailable;
    case Coin::Outgoing:
        return UtxoViewStatus::Outgoing;
    case Coin::Incoming:
        return UtxoViewStatus::Incoming;
    case Coin::Spent:
        return UtxoViewStatus::Spent;
    default:
        assert(false && "Unknown key type");
    }

    return UtxoViewStatus::Undefined;
}

UtxoViewType::EnType UtxoItem::type() const
{
    switch (_coin.m_ID.m_Type)
    {
    case Key::Type::Comission: return UtxoViewType::Comission;
    case Key::Type::Coinbase: return UtxoViewType::Coinbase;
    case Key::Type::Regular: return UtxoViewType::Regular;
    case Key::Type::Change: return UtxoViewType::Change;
    case Key::Type::Treasury: return UtxoViewType::Treasury;
    }

    return UtxoViewType::Undefined;
}

beam::Amount UtxoItem::rawAmount() const
{
    return _coin.m_ID.m_Value;
}

const beam::wallet::Coin::ID& UtxoItem::get_ID() const
{
    return _coin.m_ID;
}

beam::Height UtxoItem::rawMaturity() const
{
    return _coin.get_Maturity();
}
