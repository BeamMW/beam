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

#pragma once

#include <string>
#include <QObject>

#include "model/wallet_model.h"
#include "model/swap_coin_client_model.h"
#include "viewmodel/status_holder.h"
#include "viewmodel/wallet/transactions_list.h"
#include "swap_offers_list.h"

using namespace beam::wallet;

class SwapOffersViewModel : public QObject
{
	Q_OBJECT
    Q_PROPERTY(QAbstractItemModel*  transactions     READ getTransactions    NOTIFY allTransactionsChanged)
    Q_PROPERTY(QAbstractItemModel*  allOffers        READ getAllOffers       NOTIFY allOffersChanged)
    Q_PROPERTY(double               beamAvailable    READ beamAvailable      NOTIFY stateChanged)
    Q_PROPERTY(double               btcAvailable     READ btcAvailable       NOTIFY stateChanged)
    Q_PROPERTY(double               ltcAvailable     READ ltcAvailable       NOTIFY stateChanged)
    Q_PROPERTY(double               qtumAvailable    READ qtumAvailable      NOTIFY stateChanged)
    Q_PROPERTY(bool                 btcOK            READ btcOK              NOTIFY stateChanged)
    Q_PROPERTY(bool                 ltcOK            READ ltcOK              NOTIFY stateChanged)
    Q_PROPERTY(bool                 qtumOK           READ qtumOK             NOTIFY stateChanged)
    Q_PROPERTY(bool                 showBetaWarning  READ showBetaWarning)
    Q_PROPERTY(int                  activeTxCount    READ getActiveTxCount   NOTIFY stateChanged)
    Q_PROPERTY(bool                 hasBtcTx         READ hasBtcTx           NOTIFY stateChanged)
    Q_PROPERTY(bool                 hasLtcTx         READ hasLtcTx           NOTIFY stateChanged)
    Q_PROPERTY(bool                 hasQtumTx        READ hasQtumTx          NOTIFY stateChanged)

public:
    SwapOffersViewModel();
    virtual ~SwapOffersViewModel();

    QAbstractItemModel* getTransactions();
    QAbstractItemModel* getAllOffers();
    double  beamAvailable() const;
    double  btcAvailable() const;
    double  ltcAvailable() const;
    double  qtumAvailable() const;
    bool btcOK()  const;
    bool ltcOK()  const;
    bool qtumOK() const;
    bool showBetaWarning() const;
    int getActiveTxCount() const;
    bool hasBtcTx() const;
    bool hasLtcTx() const;
    bool hasQtumTx() const;

    Q_INVOKABLE int getCoinType();
    Q_INVOKABLE void setCoinType(int coinType);
    Q_INVOKABLE void cancelTx(QVariant variantTxID);
    Q_INVOKABLE void deleteTx(QVariant variantTxID);
    Q_INVOKABLE PaymentInfoItem* getPaymentInfo(QVariant variantTxID);

public slots:
    void onTransactionsDataModelChanged(
        beam::wallet::ChangeAction action,
        const std::vector<beam::wallet::TxDescription>& transactions);
    void onSwapOffersDataModelChanged(
        beam::wallet::ChangeAction action,
        const std::vector<beam::wallet::SwapOffer>& offers);
    void onSwapCoinClientChanged(beam::bitcoin::Client::Status status);
    void onSwapCoinClientChanged();

signals:
    void allTransactionsChanged();
    void allOffersChanged();
    void stateChanged();

private:
    bool hasActiveTx(const std::string& swapCoin) const;
    WalletModel& m_walletModel;
    
    AtomicSwapCoin m_coinType;

    TransactionsList m_transactionsList;
    SwapOffersList m_offersList;
    StatusHolder m_status;
    SwapCoinClientModel::Ptr m_btcClient;
    SwapCoinClientModel::Ptr m_ltcClient;
    SwapCoinClientModel::Ptr m_qtumClient;
};
