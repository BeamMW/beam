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

#include "model/app_model.h"
#include "swap_offers_view.h"
using namespace beam;
using namespace beam::wallet;
using namespace std;

SwapOffersViewModel::SwapOffersViewModel()
    :   m_walletModel{*AppModel::getInstance().getWallet()},
        m_coinType(AtomicSwapCoin::Bitcoin)
{
    LOG_INFO() << "SwapOffersViewModel created";

    connect(&m_walletModel,
            SIGNAL(txStatus(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&)),
            SLOT(onTransactionsDataModelChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&)));

    connect(&m_walletModel,
            SIGNAL(swapOffersChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::SwapOffer>&)),
            SLOT(onSwapOffersDataModelChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::SwapOffer>&)));

    m_walletModel.getAsync()->setSwapOffersCoinType(m_coinType);
    m_walletModel.getAsync()->getSwapOffers();
    m_walletModel.getAsync()->getWalletStatus();
}

SwapOffersViewModel::~SwapOffersViewModel()
{
    LOG_INFO() << "SwapOffersViewModel destroyed";
}

int SwapOffersViewModel::getCoinType()
{
    return static_cast<int>(m_coinType);
}

void SwapOffersViewModel::setCoinType(int coinType)
{
    m_coinType = static_cast<AtomicSwapCoin>(coinType);
    m_walletModel.getAsync()->setSwapOffersCoinType(m_coinType);
    m_walletModel.getAsync()->getSwapOffers();
}

QAbstractItemModel* SwapOffersViewModel::getAllOffers()
{
    return &m_offersList;
}

QAbstractItemModel* SwapOffersViewModel::getTransactions()
{
    return &m_transactionsList;
}

void SwapOffersViewModel::cancelTx(QVariant txParameters)
{
    if (!txParameters.isNull() && txParameters.isValid())
    {
        auto p = txParameters.value<beam::wallet::TxParameters>();
        auto txId = p.GetTxID();
        if (txId)
        {
            m_walletModel.getAsync()->cancelTx(txId.value());
        }
    }    
}

void SwapOffersViewModel::onTransactionsDataModelChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::TxDescription>& transactions)
{
    vector<shared_ptr<TxObject>> newTransactions;
    newTransactions.reserve(transactions.size());

    for (const auto& t : transactions)
    {
        newTransactions.push_back(make_shared<TxObject>(t));
    }

    switch (action)
    {
    case ChangeAction::Reset:
        {
            m_transactionsList.reset(newTransactions);
            break;
        }

    case ChangeAction::Removed:
        {
            // todo
        }

    case ChangeAction::Added:
        {
            m_transactionsList.insert(newTransactions);
            break;
        }
    
    case ChangeAction::Updated:
        {
            // todo
        }

    default:
        assert(false && "Unexpected action");
        break;
    }

    emit allTransactionsChanged();
}

void SwapOffersViewModel::onSwapOffersDataModelChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::SwapOffer>& offers)
{
    vector<shared_ptr<SwapOfferItem>> newOffers;
    newOffers.reserve(offers.size());

    for (const auto& offer : offers)
    {
        // Offers without PeerID don't pass validation
        WalletID walletID;
        if (offer.GetParameter(TxParameterID::PeerID, walletID))
        {
            newOffers.push_back(make_shared<SwapOfferItem>(offer, m_walletModel.isOwnAddress(walletID)));
        }
    }

    switch (action)
    {
    case ChangeAction::Reset:
        {
            m_offersList.reset(newOffers);
            break;
        }

    case ChangeAction::Added:
        {
            m_offersList.insert(newOffers);
            break;
        }
    
    default:
        assert(false && "Unexpected action");
        break;
    }
    
    emit allOffersChanged();
}
