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
#include "messages_view.h"

class TxObject : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool income           READ income       NOTIFY incomeChanged)
    Q_PROPERTY(QString date          READ date         NOTIFY dateChanged)
    Q_PROPERTY(QString user          READ user         NOTIFY userChanged)
    Q_PROPERTY(QString userName      READ userName     NOTIFY userChanged)
    Q_PROPERTY(QString displayName   READ displayName  NOTIFY displayNameChanged)
    Q_PROPERTY(QString comment       READ comment      NOTIFY commentChanged)
    Q_PROPERTY(QString amount        READ amount       NOTIFY amountChanged)
    Q_PROPERTY(QString change        READ change       NOTIFY changeChanged)
    Q_PROPERTY(QString status        READ status       NOTIFY statusChanged)
    Q_PROPERTY(bool canCancel        READ canCancel    NOTIFY statusChanged)
    Q_PROPERTY(bool canDelete        READ canDelete    NOTIFY statusChanged)
    Q_PROPERTY(QString sendingAddress READ getSendingAddress CONSTANT)
    Q_PROPERTY(QString receivingAddress READ getReceivingAddress CONSTANT)
    Q_PROPERTY(QString fee           READ getFee CONSTANT)

public:

    TxObject() = default;
    TxObject(const beam::TxDescription& tx);

    bool income() const;
    QString date() const;
    QString user() const;
    QString userName() const;
    QString displayName() const;
    QString comment() const;
    QString amount() const;
    QString change() const;
    QString status() const;
    bool canCancel() const;
    bool canDelete() const;
    QString getSendingAddress() const;
    QString getReceivingAddress() const;
    QString getFee() const;
    beam::WalletID peerId() const;

    void setUserName(const QString& name);
    void setDisplayName(const QString& name);
    void setStatus(beam::TxStatus status);

    void update(const beam::TxDescription& tx);

    const beam::TxDescription& getTxDescription() const;

signals:
    void incomeChanged();
    void dateChanged();
    void userChanged();
    void displayNameChanged();
    void commentChanged();
    void amountChanged();
    void changeChanged();
    void statusChanged();

public:
    beam::TxDescription _tx;
    QString _userName;
    QString _displayName;
};

class WalletViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString available     READ available       NOTIFY stateChanged)
    Q_PROPERTY(QString received      READ received        NOTIFY stateChanged)
    Q_PROPERTY(QString sent          READ sent            NOTIFY stateChanged)
    Q_PROPERTY(QString unconfirmed   READ unconfirmed     NOTIFY stateChanged)

    Q_PROPERTY(QString sendAmount READ sendAmount WRITE setSendAmount NOTIFY sendAmountChanged)

    Q_PROPERTY(QString feeGrothes READ feeGrothes WRITE setFeeGrothes NOTIFY feeGrothesChanged)

    Q_PROPERTY(QString receiverAddr READ getReceiverAddr WRITE setReceiverAddr NOTIFY receiverAddrChanged)
    Q_PROPERTY(QQmlListProperty<TxObject> transactions READ getTransactions NOTIFY transactionsChanged)

    Q_PROPERTY(QString walletStatusErrorMsg READ getWalletStatusErrorMsg NOTIFY stateChanged)
    Q_PROPERTY(bool isOfflineStatus READ getIsOfflineStatus WRITE setIsOfflineStatus NOTIFY isOfflineStatusChanged)
    Q_PROPERTY(bool isFailedStatus READ getIsFailedStatus WRITE setIsFailedStatus NOTIFY isFailedStatusChanged)

    Q_PROPERTY(QString syncTime READ syncTime NOTIFY stateChanged)
    Q_PROPERTY(bool isSyncInProgress READ getIsSyncInProgress WRITE setIsSyncInProgress NOTIFY isSyncInProgressChanged)
    Q_PROPERTY(int nodeSyncProgress READ getNodeSyncProgress WRITE setNodeSyncProgress NOTIFY nodeSyncProgressChanged)

    Q_PROPERTY(QString actualAvailable READ actualAvailable NOTIFY actualAvailableChanged)
    Q_PROPERTY(bool isEnoughMoney READ isEnoughMoney NOTIFY actualAvailableChanged)
    Q_PROPERTY(QString change READ change NOTIFY changeChanged)

    Q_PROPERTY(QString newReceiverAddr READ getNewReceiverAddr NOTIFY newReceiverAddrChanged)
    Q_PROPERTY(QString newReceiverAddrQR READ getNewReceiverAddrQR NOTIFY newReceiverAddrChanged)
    Q_PROPERTY(QString newReceiverName READ getNewReceiverName WRITE setNewReceiverName NOTIFY newReceiverNameChanged)

    Q_PROPERTY(QString comment READ getComment WRITE setComment NOTIFY commentChanged)
    Q_PROPERTY(QString branchName READ getBranchName CONSTANT)

    Q_PROPERTY(QString sortRole READ sortRole WRITE setSortRole)
    Q_PROPERTY(Qt::SortOrder sortOrder READ sortOrder WRITE setSortOrder)

    Q_PROPERTY(QString incomeRole READ getIncomeRole CONSTANT)
    Q_PROPERTY(QString dateRole READ getDateRole CONSTANT)
    Q_PROPERTY(QString userRole READ getUserRole CONSTANT)
    Q_PROPERTY(QString displayNameRole READ getDisplayNameRole CONSTANT)
    Q_PROPERTY(QString amountRole READ getAmountRole CONSTANT)
    Q_PROPERTY(QString statusRole READ getStatusRole CONSTANT)

    Q_PROPERTY(int defaultFeeInGroth READ getDefaultFeeInGroth CONSTANT)

public:

    Q_INVOKABLE void cancelTx(TxObject* pTxObject);
    Q_INVOKABLE void deleteTx(TxObject* pTxObject);
    Q_INVOKABLE void generateNewAddress();
    Q_INVOKABLE void saveNewAddress();
    Q_INVOKABLE void copyToClipboard(const QString& text);
    Q_INVOKABLE bool isValidReceiverAddress(const QString& value);

public:
    using TxList = QList<TxObject*>;

    WalletViewModel();
    virtual ~WalletViewModel();

    QString available() const;
    QString received() const;
    QString sent() const;
    QString unconfirmed() const;

    QQmlListProperty<TxObject> getTransactions();
    QString sendAmount() const;
    QString feeGrothes() const;
    QString syncTime() const;
    bool getIsSyncInProgress() const;
    void setIsSyncInProgress(bool value);
    bool getIsOfflineStatus() const;
    bool getIsFailedStatus() const;
    void setIsOfflineStatus(bool value);
    void setIsFailedStatus(bool value);
    QString getWalletStatusErrorMsg() const;

    int getNodeSyncProgress() const;
    void setNodeSyncProgress(int value);

    QString actualAvailable() const;
    bool isEnoughMoney() const;
    QString change() const;
    QString getNewReceiverAddr() const;
    QString getNewReceiverAddrQR() const;
    void setNewReceiverName(const QString& value);
    QString getNewReceiverName() const;

    QString getReceiverAddr() const;
    void setReceiverAddr(const QString& value);

    void setSendAmount(const QString& text);
    void setFeeGrothes(const QString& text);
    void setComment(const QString& value);
    QString getComment() const;
    QString getBranchName() const;
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

    int getDefaultFeeInGroth() const;

public slots:
    void onStatus(const WalletStatus& amount);
    void onTxStatus(beam::ChangeAction action, const std::vector<beam::TxDescription>& items);
    void sendMoney();
    void syncWithNode();
    void onSyncProgressUpdated(int done, int total);
    void onNodeSyncProgressUpdated(int done, int total);
    void onChangeCalculated(beam::Amount change);
    void onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID);
    void onAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses);
    void onGeneratedNewAddress(const beam::WalletAddress& addr);
    void onNodeConnectionChanged(bool isNodeConnected);
    void onNodeConnectionFailed();

signals:
    void stateChanged();

    void sendAmountChanged();
    void feeGrothesChanged();
    void transactionsChanged();
    void actualAvailableChanged();
    void changeChanged();
    void receiverAddrChanged();
    void isSyncInProgressChanged();
    void nodeSyncProgressChanged();
    void newReceiverAddrChanged();
    void newReceiverNameChanged();
    void commentChanged();

    void isOfflineStatusChanged();
    void isFailedStatusChanged();
private:
    beam::Amount calcSendAmount() const;
    beam::Amount calcFeeAmount() const;
    beam::Amount calcTotalAmount() const;

    void sortTx();

    std::function<bool(const TxObject*, const TxObject*)> generateComparer();

private:

    WalletModel& _model;

    WalletStatus _status ;

    QString _sendAmount;
    QString _feeGrothes;

    beam::Amount _change;

    TxList _txList;

    QString _receiverAddr;
    beam::WalletAddress _newReceiverAddr;
    QString _newReceiverAddrQR;
    QString _newReceiverName;
    QString _comment;

    bool _isSyncInProgress;
    bool _isOfflineStatus;
    bool _isFailedStatus;
    int _nodeDone;
    int _nodeTotal;
    int _nodeSyncProgress;
    Qt::SortOrder _sortOrder;
    QString _sortRole;
};
