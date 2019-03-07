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

class PaymentInfoItem;

class TxObject : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool income              READ income              NOTIFY incomeChanged)
    Q_PROPERTY(QString date             READ date                NOTIFY dateChanged)
    Q_PROPERTY(QString user             READ user                NOTIFY userChanged)
    Q_PROPERTY(QString userName         READ userName            NOTIFY userChanged)
    Q_PROPERTY(QString displayName      READ displayName         NOTIFY displayNameChanged)
    Q_PROPERTY(QString comment          READ comment             NOTIFY commentChanged)
    Q_PROPERTY(QString amount           READ amount              NOTIFY amountChanged)
    Q_PROPERTY(QString change           READ change              NOTIFY changeChanged)
    Q_PROPERTY(QString status           READ status              NOTIFY statusChanged)
    Q_PROPERTY(bool canCancel           READ canCancel           NOTIFY statusChanged)
    Q_PROPERTY(bool canDelete           READ canDelete           NOTIFY statusChanged)
    Q_PROPERTY(QString sendingAddress   READ getSendingAddress   CONSTANT)
    Q_PROPERTY(QString receivingAddress READ getReceivingAddress CONSTANT)
    Q_PROPERTY(QString fee              READ getFee              CONSTANT)
    Q_PROPERTY(QString kernelID         READ getKernelID         WRITE setKernelID  NOTIFY kernelIDChanged)
    Q_PROPERTY(QString failureReason    READ getFailureReason    NOTIFY failureReasonChanged)
    Q_PROPERTY(bool hasPaymentProof     READ hasPaymentProof     NOTIFY kernelIDChanged)

public:

    TxObject(QObject* parent = nullptr);
    TxObject(const beam::TxDescription& tx, QObject* parent = nullptr);

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
    QString getKernelID() const;
    void setKernelID(const QString& value);
    QString getFailureReason() const;
    bool hasPaymentProof() const;

    void setUserName(const QString& name);
    void setDisplayName(const QString& name);
    void setStatus(beam::TxStatus status);
    void setFailureReason(beam::TxFailureReason reason);

    void update(const beam::TxDescription& tx);

    const beam::TxDescription& getTxDescription() const;

    Q_INVOKABLE bool inProgress() const;
    Q_INVOKABLE bool isCompleted() const;
    Q_INVOKABLE bool isSelfTx() const;
    Q_INVOKABLE PaymentInfoItem* getPaymentInfo();

signals:
    void incomeChanged();
    void dateChanged();
    void userChanged();
    void displayNameChanged();
    void commentChanged();
    void amountChanged();
    void changeChanged();
    void statusChanged();
    void kernelIDChanged();
    void failureReasonChanged();
private:
    beam::TxDescription m_tx;
    QString m_userName;
    QString m_displayName;
    QString m_kernelID;
};

class PaymentInfoItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString sender              READ getSender              NOTIFY paymentProofChanged)
    Q_PROPERTY(QString receiver            READ getReceiver            NOTIFY paymentProofChanged)
    Q_PROPERTY(QString amount              READ getAmount              NOTIFY paymentProofChanged)
    Q_PROPERTY(QString kernelID            READ getKernelID            NOTIFY paymentProofChanged)
    Q_PROPERTY(bool isValid                READ isValid                NOTIFY paymentProofChanged)
    Q_PROPERTY(QString paymentProof        READ getPaymentProof WRITE setPaymentProof NOTIFY paymentProofChanged )

public:
    PaymentInfoItem(QObject* parent = nullptr);
    QString getSender() const;
    QString getReceiver() const;
    QString getAmount() const;
    QString getKernelID() const;
    bool isValid() const;
    QString getPaymentProof() const;
    void setPaymentProof(const QString& value);

    Q_INVOKABLE void reset();
signals:
    void paymentProofChanged();
private:
    QString m_paymentProof;
    beam::wallet::PaymentInfo m_paymentInfo;
};

class MyPaymentInfoItem : public PaymentInfoItem
{
    Q_OBJECT
public:
    MyPaymentInfoItem(const beam::TxID& txID, QObject* parent = nullptr);
private slots:
    void onPaymentProofExported(const beam::TxID& txID, const QString& proof);
};

class WalletViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString available   READ available    NOTIFY stateChanged)
    Q_PROPERTY(QString receiving   READ receiving    NOTIFY stateChanged)
    Q_PROPERTY(QString sending     READ sending      NOTIFY stateChanged)
    Q_PROPERTY(QString maturing    READ maturing     NOTIFY stateChanged)

    Q_PROPERTY(QString sendAmount READ sendAmount WRITE setSendAmount NOTIFY sendAmountChanged)
    Q_PROPERTY(QString amountMissingToSend READ getAmountMissingToSend NOTIFY actualAvailableChanged)

    Q_PROPERTY(QString feeGrothes READ feeGrothes WRITE setFeeGrothes NOTIFY feeGrothesChanged)

    Q_PROPERTY(QString receiverAddr READ getReceiverAddr WRITE setReceiverAddr NOTIFY receiverAddrChanged)
    Q_PROPERTY(QQmlListProperty<TxObject> transactions READ getTransactions NOTIFY transactionsChanged)

    Q_PROPERTY(QString actualAvailable READ actualAvailable NOTIFY actualAvailableChanged)
    Q_PROPERTY(bool isEnoughMoney READ isEnoughMoney NOTIFY actualAvailableChanged)
    Q_PROPERTY(QString change READ change NOTIFY changeChanged)

    Q_PROPERTY(QString newReceiverAddr READ getNewReceiverAddr NOTIFY newReceiverAddrChanged)
    Q_PROPERTY(QString newReceiverAddrQR READ getNewReceiverAddrQR NOTIFY newReceiverAddrChanged)
    Q_PROPERTY(QString newReceiverName READ getNewReceiverName WRITE setNewReceiverName NOTIFY newReceiverNameChanged)

    Q_PROPERTY(QString comment READ getComment WRITE setComment NOTIFY commentChanged)

    Q_PROPERTY(QString sortRole READ sortRole WRITE setSortRole)
    Q_PROPERTY(Qt::SortOrder sortOrder READ sortOrder WRITE setSortOrder)

    Q_PROPERTY(QString incomeRole READ getIncomeRole CONSTANT)
    Q_PROPERTY(QString dateRole READ getDateRole CONSTANT)
    Q_PROPERTY(QString userRole READ getUserRole CONSTANT)
    Q_PROPERTY(QString displayNameRole READ getDisplayNameRole CONSTANT)
    Q_PROPERTY(QString amountRole READ getAmountRole CONSTANT)
    Q_PROPERTY(QString statusRole READ getStatusRole CONSTANT)

    Q_PROPERTY(int defaultFeeInGroth READ getDefaultFeeInGroth CONSTANT)

    Q_PROPERTY(int expires READ getExpires WRITE setExpires NOTIFY expiresChanged)

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
    QString receiving() const;
    QString sending() const;
    QString maturing() const;

    QQmlListProperty<TxObject> getTransactions();
    QString sendAmount() const;
    QString getAmountMissingToSend() const;
    QString feeGrothes() const;
    bool getIsOfflineStatus() const;
    bool getIsFailedStatus() const;
    QString getWalletStatusErrorMsg() const;

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

    void setExpires(int value);
    int getExpires() const;

public slots:
    void onStatus(const WalletStatus& amount);
    void onTxStatus(beam::ChangeAction action, const std::vector<beam::TxDescription>& items);
    void sendMoney();
    void syncWithNode();
    void onChangeCalculated(beam::Amount change);
    void onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID);
    void onAddresses(bool own, const std::vector<beam::WalletAddress>& addresses);
    void onGeneratedNewAddress(const beam::WalletAddress& addr);
    void onSendMoneyVerified();
    void onCantSendToExpired();

signals:
    void stateChanged();

    void sendAmountChanged();
    void feeGrothesChanged();
    void transactionsChanged();
    void actualAvailableChanged();
    void changeChanged();
    void receiverAddrChanged();
    void newReceiverAddrChanged();
    void newReceiverNameChanged();
    void commentChanged();
    void expiresChanged();
    void sendMoneyVerified();
    void cantSendToExpired();

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

    Qt::SortOrder _sortOrder;
    QString _sortRole;
    int _expires;
};
