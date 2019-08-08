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
#include "model/wallet_model.h"
#include "model/settings.h"
#include "messages_view.h"
#include "status_holder.h"
#include "tx_object.h"

class WalletViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString available   READ available    NOTIFY stateChanged)
    Q_PROPERTY(QString receiving   READ receiving    NOTIFY stateChanged)
    Q_PROPERTY(QString sending     READ sending      NOTIFY stateChanged)
    Q_PROPERTY(QString maturing    READ maturing     NOTIFY stateChanged)
    Q_PROPERTY(QQmlListProperty<TxObject> transactions READ getTransactions NOTIFY transactionsChanged)
    Q_PROPERTY(QString sortRole READ sortRole WRITE setSortRole)
    Q_PROPERTY(Qt::SortOrder sortOrder READ sortOrder WRITE setSortOrder)
    Q_PROPERTY(QString incomeRole READ getIncomeRole CONSTANT)
    Q_PROPERTY(QString dateRole READ getDateRole CONSTANT)
    Q_PROPERTY(QString userRole READ getUserRole CONSTANT)
    Q_PROPERTY(QString displayNameRole READ getDisplayNameRole CONSTANT)
    Q_PROPERTY(QString amountRole READ getAmountRole CONSTANT)
    Q_PROPERTY(QString statusRole READ getStatusRole CONSTANT)
    Q_PROPERTY(bool isAllowedBeamMWLinks READ isAllowedBeamMWLinks WRITE allowBeamMWLinks NOTIFY beamMWLinksAllowed)

public:
    Q_INVOKABLE void cancelTx(TxObject* pTxObject);
    Q_INVOKABLE void deleteTx(TxObject* pTxObject);

public:
    using TxList = QList<TxObject*>;

    WalletViewModel();
    virtual ~WalletViewModel();

    QString available() const;
    QString receiving() const;
    QString sending() const;
    QString maturing() const;

    QQmlListProperty<TxObject> getTransactions();
    bool getIsOfflineStatus() const;
    bool getIsFailedStatus() const;
    QString getWalletStatusErrorMsg() const;

    QString sortRole() const;
    void setSortRole(const QString&);
    Qt::SortOrder sortOrder() const;
    void setSortOrder(Qt::SortOrder);
    QString getIncomeRole() const;
    QString getDateRole() const;
    QString getUserRole() const;
    QString getDisplayNameRole() const;
    QString getAmountRole() const;
    QString getStatusRole() const;

    Q_INVOKABLE bool isAllowedBeamMWLinks() const;
    void allowBeamMWLinks(bool value);

public slots:
    void onTxStatus(beam::wallet::ChangeAction action, const std::vector<beam::wallet::TxDescription>& items);
    void onAddresses(bool own, const std::vector<beam::wallet::WalletAddress>& addresses);

signals:
    void stateChanged();
    void transactionsChanged();
    void beamMWLinksAllowed();

private:
    void sortTx();
    std::function<bool(const TxObject*, const TxObject*)> generateComparer();

private:
    WalletModel& _model;
    WalletSettings& _settings;
    StatusHolder _status;
    TxList _txList;

    Qt::SortOrder _sortOrder;
    QString _sortRole;
};
