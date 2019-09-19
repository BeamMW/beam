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
#include "model/settings.h"
#include "swap_offers_view.h"
using namespace beam;
using namespace beam::wallet;
using namespace std;
using namespace beam::bitcoin;

SwapOffersViewModel::SwapOffersViewModel()
    :   m_walletModel{*AppModel::getInstance().getWallet()},
        m_coinType(AtomicSwapCoin::Bitcoin),
        m_btcClient(AppModel::getInstance().getBitcoinClient()),
        m_ltcClient(AppModel::getInstance().getLitecoinClient()),
        m_qtumClient(AppModel::getInstance().getQtumClient())
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

    m_status.setOnChanged([this]() {
        emit stateChanged();
    });

    connect(m_btcClient.get(),  SIGNAL(stateChanged()),
            this, SLOT(onSwapCoinClientChanged()));
    connect(m_ltcClient.get(), SIGNAL(stateChanged()),
            this, SLOT(onSwapCoinClientChanged()));
    connect(m_qtumClient.get(), SIGNAL(stateChanged()),
            this, SLOT(onSwapCoinClientChanged()));
    connect(m_btcClient.get(),
            SIGNAL(gotStatus(beam::bitcoin::Client::Status)),
            this,
            SLOT(onSwapCoinClientChanged(beam::bitcoin::Client::Status)));
    connect(m_ltcClient.get(),
            SIGNAL(gotStatus(beam::bitcoin::Client::Status)),
            this,
            SLOT(onSwapCoinClientChanged(beam::bitcoin::Client::Status)));
    connect(m_qtumClient.get(),
            SIGNAL(gotStatus(beam::bitcoin::Client::Status)),
            this,
            SLOT(onSwapCoinClientChanged(beam::bitcoin::Client::Status)));

    m_status.refresh();
}

SwapOffersViewModel::~SwapOffersViewModel()
{
    disconnect(m_btcClient.get(), 0, this, 0);
    disconnect(m_ltcClient.get(), 0, this, 0);
    disconnect(m_qtumClient.get(), 0, this, 0);

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

double  SwapOffersViewModel::beamAvailable() const
{
    return double(int64_t(m_status.getAvailable())) / Rules::Coin;
}

double  SwapOffersViewModel::btcAvailable() const
{
    return m_btcClient->getAvailable();
}

double  SwapOffersViewModel::ltcAvailable() const
{
    return m_ltcClient->getAvailable();
}

double  SwapOffersViewModel::qtumAvailable() const
{
    return m_qtumClient->getAvailable();
}

bool SwapOffersViewModel::btcOK()  const
{
    return m_btcClient->getStatus() == Client::Status::Connected;
}

bool SwapOffersViewModel::ltcOK()  const
{
    return m_ltcClient->getStatus() == Client::Status::Connected;
}

bool SwapOffersViewModel::qtumOK() const
{
    return m_qtumClient->getStatus() == Client::Status::Connected;
}

QAbstractItemModel* SwapOffersViewModel::getTransactions()
{
    return &m_transactionsList;
}

void SwapOffersViewModel::cancelTx(QVariant variantTxID)
{
    if (!variantTxID.isNull() && variantTxID.isValid())
    {
        auto txId = variantTxID.value<beam::wallet::TxID>();
        m_walletModel.getAsync()->cancelTx(txId);
        m_walletModel.getAsync()->cancelOffer(txId);
    }
}

void SwapOffersViewModel::deleteTx(QVariant variantTxID)
{
    if (!variantTxID.isNull() && variantTxID.isValid())
    {
        auto txId = variantTxID.value<beam::wallet::TxID>();
        m_walletModel.getAsync()->deleteTx(txId);
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
            break;
        }

    case ChangeAction::Added:
        {
            m_transactionsList.insert(newTransactions);
            break;
        }
    
    case ChangeAction::Updated:
        {
            // todo
            break;
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
    case ChangeAction::Removed:
        {
            m_offersList.remove(newOffers);
            break;
        }
    
    default:
        assert(false && "Unexpected action");
        break;
    }
    
    emit allOffersChanged();
}

void SwapOffersViewModel::onSwapCoinClientChanged(Client::Status status)
{
    onSwapCoinClientChanged();
}

void SwapOffersViewModel::onSwapCoinClientChanged()
{
    emit stateChanged();
}

bool SwapOffersViewModel::showBetaWarning() const
{
    auto& settings = AppModel::getInstance().getSettings();
    bool showWarning = settings.showSwapBetaWarning();
    if (showWarning)
    {
        settings.setShowSwapBetaWarning(false);
    }
    return showWarning;
}
