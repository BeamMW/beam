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
#include <QDateTime>
#include "viewmodel/payment_item.h"
#include "viewmodel/ui_helpers.h"

class TxObject : public QObject
{
    Q_OBJECT

public:
    TxObject(QObject* parent = nullptr);
    TxObject(const beam::wallet::TxDescription& tx, QObject* parent = nullptr);

    auto timeCreated() const -> beam::Timestamp;
    auto getTxID() const -> beam::wallet::TxID;
    auto getAmountWithCurrency() const->QString;
    auto getAmount() const -> QString;
    auto getAmountValue() const -> beam::Amount;
    auto getComment() const -> QString;
    auto getAddressFrom() const -> QString;
    auto getAddressTo() const -> QString;
    virtual auto getFee() const -> QString;
    auto getKernelID() const -> QString;
    auto getTransactionID() const -> QString;
    auto hasPaymentProof() const -> bool;
    virtual auto getStatus() const -> QString;
    virtual auto getFailureReason() const -> QString;

    bool isIncome() const;
    bool isSelfTx() const;
    virtual bool isCancelAvailable() const;
    virtual bool isDeleteAvailable() const;
    virtual bool isInProgress() const;
    virtual bool isPending() const;
    virtual bool isExpired() const;
    virtual bool isCompleted() const;
    virtual bool isCanceled() const;
    virtual bool isFailed() const;

    void setKernelID(const QString& value);
    void setStatus(beam::wallet::TxStatus status);
    void setFailureReason(beam::wallet::TxFailureReason reason);
    void update(const beam::wallet::TxDescription& tx);

signals:
    void statusChanged();
    void kernelIDChanged();
    void failureReasonChanged();

protected:
    auto getTxDescription() const -> const beam::wallet::TxDescription&;
    auto getReasonString(beam::wallet::TxFailureReason reason) const -> QString;
 
    beam::wallet::TxDescription m_tx;
    QString m_kernelID;
    beam::wallet::TxType m_type;
};
