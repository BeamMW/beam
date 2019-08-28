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

#include "swap_offers_list.h"

SwapOffersList::SwapOffersList()
{
}

QHash<int, QByteArray> SwapOffersList::roleNames() const
{
    static const auto roles = QHash<int, QByteArray>
    {
        { static_cast<int>(Roles::TimeCreatedRole), "timeCreated" },
        { static_cast<int>(Roles::TimeCreatedSortRole), "timeCreatedSort" },
        { static_cast<int>(Roles::AmountSendRole), "amountSend" },
        { static_cast<int>(Roles::AmountSendSortRole), "amountSendSort" },
        { static_cast<int>(Roles::AmountReceiveRole), "amountReceive" },
        { static_cast<int>(Roles::AmountReceiveSortRole), "amountReceiveSort" },
        { static_cast<int>(Roles::RateRole), "rate" },
        { static_cast<int>(Roles::RateSortRole), "rateSort" },
        { static_cast<int>(Roles::ExpirationRole), "expiration" },
        { static_cast<int>(Roles::ExpirationSortRole), "expirationSort" },
        { static_cast<int>(Roles::IsOwnOfferRole), "isOwnOffer" },
        { static_cast<int>(Roles::RawTxParametersRole), "rawTxParameters" }
    };
    return roles;
}

QVariant SwapOffersList::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_list.size())
    {
       return QVariant();
    }
    auto& value = m_list[index.row()];
    switch (static_cast<Roles>(role))
    {
    case Roles::TimeCreatedRole:
    case Roles::TimeCreatedSortRole:
        return value->timeCreated();
	case Roles::AmountSendRole:
        return value->amountSend();
    case Roles::AmountSendSortRole:
        return static_cast<uint>(value->rawAmountSend());
	case Roles::AmountReceiveRole:
        return value->amountReceive();
    case Roles::AmountReceiveSortRole:
        return static_cast<uint>(value->rawAmountReceive());
    case Roles::RateRole:
    case Roles::RateSortRole:
        return value->rate();
    case Roles::ExpirationRole:
    case Roles::ExpirationSortRole:
        return value->timeExpiration();
    case Roles::IsOwnOfferRole:
        return value->isOwnOffer();
    case Roles::RawTxParametersRole:
		{
			auto txParams = value->getTxParameters();
			return QVariant::fromValue(txParams);
		}
    default:
        return QVariant();
    }
}
