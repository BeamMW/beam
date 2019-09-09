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

#include <QObject>
#include "model/wallet_model.h"
#include "viewmodel/wallet/transactions_list.h"
#include "swap_offers_list.h"

using namespace beam::wallet;

class SwapOffersViewModel : public QObject
{
	Q_OBJECT
    Q_PROPERTY(QAbstractItemModel*  transactions    READ getTransactions    NOTIFY allTransactionsChanged)
    Q_PROPERTY(QAbstractItemModel*  allOffers       READ getAllOffers       NOTIFY allOffersChanged)

public:
    SwapOffersViewModel();
    virtual ~SwapOffersViewModel();

    QAbstractItemModel* getTransactions();
    QAbstractItemModel* getAllOffers();

    Q_INVOKABLE int getCoinType();
    Q_INVOKABLE void setCoinType(int coinType);
    Q_INVOKABLE void cancelTx(QVariant txParameters);

public slots:
    void onTransactionsDataModelChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::TxDescription>& transactions);
    void onSwapOffersDataModelChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::SwapOffer>& offers);

signals:
    void allTransactionsChanged();
    void allOffersChanged();

private:
    WalletModel& m_walletModel;
    
    AtomicSwapCoin m_coinType;

    TransactionsList m_transactionsList;
    SwapOffersList m_offersList;

};
