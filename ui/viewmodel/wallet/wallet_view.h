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
#pragma once

#include <QObject>
#include <QQmlListProperty>
#include "wallet/bitcoin/client.h"
#include "model/wallet_model.h"
#include "model/settings.h"
#include "viewmodel/messages_view.h"
#include "viewmodel/status_holder.h"
#include "transactions_list.h"

class WalletViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double beamAvailable                 READ beamAvailable              NOTIFY stateChanged)
    Q_PROPERTY(double beamReceiving                 READ beamReceiving              NOTIFY stateChanged)
    Q_PROPERTY(double beamSending                   READ beamSending                NOTIFY stateChanged)
    Q_PROPERTY(double beamLocked                    READ beamLocked                 NOTIFY stateChanged)
    Q_PROPERTY(double beamLockedMaturing            READ beamLockedMaturing         NOTIFY stateChanged)
    Q_PROPERTY(double beamReceivingChange           READ beamReceivingChange        NOTIFY stateChanged)
    Q_PROPERTY(double beamReceivingIncoming         READ beamReceivingIncoming      NOTIFY stateChanged)
    Q_PROPERTY(bool isAllowedBeamMWLinks            READ isAllowedBeamMWLinks       WRITE allowBeamMWLinks      NOTIFY beamMWLinksAllowed)
    Q_PROPERTY(QAbstractItemModel* transactions     READ getTransactions            NOTIFY transactionsChanged)

public:
    WalletViewModel();

    double  beamAvailable() const;
    double  beamReceiving() const;
    double  beamSending() const;
    double  beamLocked() const;
    double  beamLockedMaturing() const;
    double  beamReceivingChange() const;
    double  beamReceivingIncoming() const;

    QAbstractItemModel* getTransactions();
    bool getIsOfflineStatus() const;
    bool getIsFailedStatus() const;
    QString getWalletStatusErrorMsg() const;
    void allowBeamMWLinks(bool value);

    Q_INVOKABLE void cancelTx(QVariant variantTxID);
    Q_INVOKABLE void deleteTx(QVariant variantTxID);
    Q_INVOKABLE PaymentInfoItem* getPaymentInfo(QVariant variantTxID);
    Q_INVOKABLE bool isAllowedBeamMWLinks() const;

public slots:
    void onTxStatus(beam::wallet::ChangeAction action, const std::vector<beam::wallet::TxDescription>& items);

signals:
    void stateChanged();
    void transactionsChanged();
    void beamMWLinksAllowed();

private:
    WalletModel& _model;
    WalletSettings& _settings;
    StatusHolder _status;
    TransactionsList _transactionsList;
};
