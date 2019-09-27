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

#include <qdebug.h>
#include "model/app_model.h"
#include "model/settings.h"
#include "swap_offers_view.h"
#include "viewmodel/ui_helpers.h"

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

PaymentInfoItem* SwapOffersViewModel::getPaymentInfo(QVariant variantTxID)
{
    if (!variantTxID.isNull() && variantTxID.isValid())
    {
        auto txId = variantTxID.value<beam::wallet::TxID>();
        return new MyPaymentInfoItem(txId, this);
    }
    else return Q_NULLPTR;
}

void SwapOffersViewModel::onTransactionsDataModelChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::TxDescription>& transactions)
{
    vector<shared_ptr<TxObject>> modifiedTransactions;
    modifiedTransactions.reserve(transactions.size());

    for (const auto& t : transactions)
    {
        if (t.GetParameter<TxType>(TxParameterID::TransactionType) == TxType::AtomicSwap)
        {
            modifiedTransactions.push_back(make_shared<TxObject>(t));
        }
    }

    switch (action)
    {
        case ChangeAction::Reset:
            {
                m_transactionsList.reset(modifiedTransactions);
                break;
            }

        case ChangeAction::Removed:
            {
                m_transactionsList.remove(modifiedTransactions);
                break;
            }

        case ChangeAction::Added:
            {
                m_transactionsList.insert(modifiedTransactions);
                break;
            }
        
        case ChangeAction::Updated:
            {
                m_transactionsList.update(modifiedTransactions);
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
    vector<shared_ptr<SwapOfferItem>> modifiedOffers;
    modifiedOffers.reserve(offers.size());

    for (const auto& offer : offers)
    {
        // Offers without PeerID don't pass validation
        WalletID walletID;
        if (offer.GetParameter(TxParameterID::PeerID, walletID))
        {
            QDateTime timeExpiration;

            auto peerResponseTime = offer.GetParameter<beam::Height>(beam::wallet::TxParameterID::PeerResponseTime);
            auto minHeight = offer.GetParameter<beam::Height>(beam::wallet::TxParameterID::MinHeight);
            auto currentHeight = m_status.getCurrentHeight();

            if (currentHeight && peerResponseTime && minHeight)
            {
                auto expiresHeight = *minHeight + *peerResponseTime;
                timeExpiration = beamui::CalculateExpiresTime(currentHeight, expiresHeight);
            }

            modifiedOffers.push_back(make_shared<SwapOfferItem>(offer, m_walletModel.isOwnAddress(walletID), timeExpiration));
        }
    }

    switch (action)
    {
        case ChangeAction::Reset:
            {
                m_offersList.reset(modifiedOffers);
                break;
            }

        case ChangeAction::Added:
            {
                m_offersList.insert(modifiedOffers);
                break;
            }

        case ChangeAction::Removed:
            {
                m_offersList.remove(modifiedOffers);
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

int SwapOffersViewModel::getActiveTxCount() const
{
    int count = 0;
    for (int i = 0; i < m_transactionsList.rowCount(); ++i)
    {
        auto index = m_transactionsList.index(i, 0);
        try
        {
            bool isInProgress = m_transactionsList.data(
                index,
                static_cast<int>(TransactionsList::Roles::IsInProgress))
                .toBool();
            if (isInProgress)
            {
                ++count;
            }
        }
        catch(...)
        {
            qDebug() << "Wrong ROLE data";
        }
    }

    return count;
}

bool SwapOffersViewModel::hasBtcTx() const
{
    return hasActiveTx(toStdString(beamui::Currencies::Bitcoin));
}

bool SwapOffersViewModel::hasLtcTx() const
{
    return hasActiveTx(toStdString(beamui::Currencies::Bitcoin));
}

bool SwapOffersViewModel::hasQtumTx() const
{
    return hasActiveTx(toStdString(beamui::Currencies::Bitcoin));
}

bool SwapOffersViewModel::hasActiveTx(const std::string& swapCoin) const
{
    for (int i = 0; i < m_transactionsList.rowCount(); ++i)
    {
        auto index = m_transactionsList.index(i, 0);
        try
        {
            bool isInProgress = m_transactionsList.data(
                index,
                static_cast<int>(TransactionsList::Roles::IsInProgress))
                .toBool();
            auto mySwapCoin = m_transactionsList.data(
                index,
                static_cast<int>(TransactionsList::Roles::SwapCoin))
                .toString();
            if (isInProgress &&
                mySwapCoin.toStdString() == swapCoin)
            {
                return true;
            }
        }
        catch(...)
        {
            qDebug() << "Wrong ROLE data";
        }
    }

    return false;
}
