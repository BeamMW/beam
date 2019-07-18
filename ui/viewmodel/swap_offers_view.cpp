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

#include "model/app_model.h"
#include "ui_helpers.h"
#include "swap_offers_view.h"
using namespace beam;
using namespace beam::wallet;
using namespace std;
using namespace beamui;

QString SwapOfferItem::amount() const
{
    return BeamToString(m_offer.m_amount);
}

QString SwapOfferItem::status() const
{
    return m_offer.getStatusString().c_str();
}

beam::Amount SwapOfferItem::rawAmount() const
{
    return m_offer.m_amount;
}

QHash<int, QByteArray> SwapOffersList::roleNames() const
{
    static const auto roles = QHash<int, QByteArray>
    {
        { static_cast<int>(Roles::AmountRole), "amount" },
        { static_cast<int>(Roles::AmountSortRole), "amountSort" },
        { static_cast<int>(Roles::StatusRole), "status" },
        { static_cast<int>(Roles::StatusSortRole), "statusSort" }
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
	case Roles::AmountRole:
        return value->amount();
    case Roles::AmountSortRole:
        return value->rawAmount();
    case Roles::StatusRole:
    case Roles::StatusSortRole:
        return value->status();
    default:
        return QVariant();
    }
}

SwapOffersViewModel::SwapOffersViewModel()
    : m_walletModel{*AppModel::getInstance().getWallet()}
{
    connect(&m_walletModel,
            SIGNAL(swapOffersChanged(const std::vector<beam::wallet::TxDescription> & offers)),
            SLOT(onAllOffersChanged(const std::vector<beam::wallet::TxDescription> & offers)));

    m_walletModel.getAsync()->getSwapOffers();
}

SwapOffersViewModel::~SwapOffersViewModel()
{
    // no raw pointers
}

QAbstractItemModel* SwapOffersViewModel::getAllOffers()
{
    return &m_offersList;
}

void SwapOffersViewModel::onAllOffersChanged(const std::vector<beam::wallet::TxDescription>& offers)
{
    vector<shared_ptr<SwapOfferItem>> newOffers(offers.size());

    for (const auto& offer : offers)
    {
        newOffers.push_back(make_shared<SwapOfferItem>(offer));
    }

    m_offersList.reset(newOffers);
}
