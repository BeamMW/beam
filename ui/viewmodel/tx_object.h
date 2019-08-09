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
#include "payment_item.h"

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
    Q_PROPERTY(QString transactionID    READ getTransactionID    CONSTANT)
    Q_PROPERTY(QString failureReason    READ getFailureReason    NOTIFY failureReasonChanged)
    Q_PROPERTY(bool hasPaymentProof     READ hasPaymentProof     NOTIFY kernelIDChanged)

public:

    TxObject(QObject* parent = nullptr);
    TxObject(const beam::wallet::TxDescription& tx, QObject* parent = nullptr);

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
    beam::wallet::WalletID peerId() const;
    QString getKernelID() const;
    void setKernelID(const QString& value);
    QString getTransactionID() const;
    QString getFailureReason() const;
    bool hasPaymentProof() const;

    void setUserName(const QString& name);
    void setDisplayName(const QString& name);
    void setStatus(beam::wallet::TxStatus status);
    void setFailureReason(beam::wallet::TxFailureReason reason);

    void update(const beam::wallet::TxDescription& tx);

    const beam::wallet::TxDescription& getTxDescription() const;

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
    beam::wallet::TxDescription m_tx;
    QString m_userName;
    QString m_displayName;
    QString m_kernelID;
};
