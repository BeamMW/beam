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
#include "wallet/wallet_db.h"

class PaymentInfoItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString sender              READ getSender              NOTIFY paymentProofChanged)
    Q_PROPERTY(QString receiver            READ getReceiver            NOTIFY paymentProofChanged)
    Q_PROPERTY(QString amount              READ getAmount              NOTIFY paymentProofChanged)
    Q_PROPERTY(QString amountValue         READ getAmountValue         NOTIFY paymentProofChanged)
    Q_PROPERTY(QString kernelID            READ getKernelID            NOTIFY paymentProofChanged)
    Q_PROPERTY(bool isValid                READ isValid                NOTIFY paymentProofChanged)
    Q_PROPERTY(QString paymentProof        READ getPaymentProof WRITE setPaymentProof NOTIFY paymentProofChanged )

public:
    PaymentInfoItem(QObject* parent = nullptr);
    QString getSender() const;
    QString getReceiver() const;
    QString getAmount() const;
    QString getAmountValue() const;
    QString getKernelID() const;
    bool isValid() const;
    QString getPaymentProof() const;
    void setPaymentProof(const QString& value);
    Q_INVOKABLE void reset();

signals:
    void paymentProofChanged();

private:
    QString m_paymentProof;
    beam::wallet::storage::PaymentInfo m_paymentInfo;
};

class MyPaymentInfoItem : public PaymentInfoItem
{
    Q_OBJECT
public:
    MyPaymentInfoItem(const beam::wallet::TxID& txID, QObject* parent = nullptr);

private slots:
    void onPaymentProofExported(const beam::wallet::TxID& txID, const QString& proof);
};
