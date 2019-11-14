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
#include "swap_offers_list.h"
#include "swap_tx_object_list.h"

using namespace beam::wallet;

class SwapOffersViewModel : public QObject
{
	Q_OBJECT
    Q_PROPERTY(QAbstractItemModel*  transactions     READ getTransactions    NOTIFY allTransactionsChanged)
    Q_PROPERTY(QAbstractItemModel*  allOffers        READ getAllOffers       NOTIFY allOffersChanged)
    Q_PROPERTY(int                  selectedCoin     READ getSelectedCoin    NOTIFY selectedCoinChanged     WRITE   setSelectedCoin)
    Q_PROPERTY(QString              beamAvailable    READ beamAvailable      NOTIFY beamAvailableChanged)
    Q_PROPERTY(QString              btcAvailable     READ btcAvailable       NOTIFY btcAvailableChanged)
    Q_PROPERTY(QString              ltcAvailable     READ ltcAvailable       NOTIFY ltcAvailableChanged)
    Q_PROPERTY(QString              qtumAvailable    READ qtumAvailable      NOTIFY qtumAvailableChanged)
    Q_PROPERTY(bool                 btcOK            READ btcOK              NOTIFY btcOKChanged)
    Q_PROPERTY(bool                 ltcOK            READ ltcOK              NOTIFY ltcOKChanged)
    Q_PROPERTY(bool                 qtumOK           READ qtumOK             NOTIFY qtumOKChanged)
    Q_PROPERTY(bool                 btcConnecting    READ btcConnecting      NOTIFY btcOKChanged)
    Q_PROPERTY(bool                 ltcConnecting    READ ltcConnecting      NOTIFY ltcOKChanged)
    Q_PROPERTY(bool                 qtumConnecting   READ qtumConnecting     NOTIFY qtumOKChanged)
    Q_PROPERTY(bool                 showBetaWarning  READ showBetaWarning)
    Q_PROPERTY(int                  activeTxCount    READ getActiveTxCount   NOTIFY allTransactionsChanged)
    Q_PROPERTY(bool                 hasBtcTx         READ hasBtcTx           NOTIFY allTransactionsChanged)
    Q_PROPERTY(bool                 hasLtcTx         READ hasLtcTx           NOTIFY allTransactionsChanged)
    Q_PROPERTY(bool                 hasQtumTx        READ hasQtumTx          NOTIFY allTransactionsChanged)

public:
    SwapOffersViewModel();

    QAbstractItemModel* getTransactions();
    QAbstractItemModel* getAllOffers();
    int getSelectedCoin();
    void setSelectedCoin(int coinType);
    QString beamAvailable() const;
    QString btcAvailable() const;
    QString ltcAvailable() const;
    QString qtumAvailable() const;
    bool btcOK()  const;
    bool ltcOK()  const;
    bool qtumOK() const;
    bool btcConnecting() const;
    bool ltcConnecting() const;
    bool qtumConnecting() const;
    bool showBetaWarning() const;
    int getActiveTxCount() const;
    bool hasBtcTx() const;
    bool hasLtcTx() const;
    bool hasQtumTx() const;

    Q_INVOKABLE void cancelOffer(const QVariant& variantTxID);
    Q_INVOKABLE void cancelTx(const QVariant& variantTxID);
    Q_INVOKABLE void deleteTx(const QVariant& variantTxID);
    Q_INVOKABLE PaymentInfoItem* getPaymentInfo(const QVariant& variantTxID);

public slots:
    void onTransactionsDataModelChanged(
        beam::wallet::ChangeAction action,
        const std::vector<beam::wallet::TxDescription>& transactions);
    void onSwapOffersDataModelChanged(
        beam::wallet::ChangeAction action,
        const std::vector<beam::wallet::SwapOffer>& offers);

signals:
    void allTransactionsChanged();
    void allOffersChanged();
    void selectedCoinChanged();
    void beamAvailableChanged();
    void btcAvailableChanged();
    void ltcAvailableChanged();
    void qtumAvailableChanged();
    void btcOKChanged();
    void ltcOKChanged();
    void qtumOKChanged();
    void offerRemovedFromTable(QVariant variantTxID);

private:
    bool hasActiveTx(const std::string& swapCoin) const;
    WalletModel& m_walletModel;
    
    AtomicSwapCoin m_selectedCoin;

    SwapTxObjectList m_transactionsList;
    SwapOffersList m_offersList;
    SwapCoinClientModel::Ptr m_btcClient;
    SwapCoinClientModel::Ptr m_ltcClient;
    SwapCoinClientModel::Ptr m_qtumClient;
};
