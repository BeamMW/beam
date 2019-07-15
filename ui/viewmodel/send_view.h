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
#include "model/wallet_model.h"
#include "status_holder.h"

class SendViewModel: public QObject
{
    Q_OBJECT
    Q_PROPERTY(int      feeGrothes         READ getFeeGrothes         WRITE setFeeGrothes       NOTIFY feeGrothesChanged)
    Q_PROPERTY(double   sendAmount         READ getSendAmount         WRITE setSendAmount       NOTIFY sendAmountChanged)
    Q_PROPERTY(QString  comment            READ getComment            WRITE setComment          NOTIFY commentChanged)
    Q_PROPERTY(QString  receiverAddress    READ getReceiverAddress    WRITE setReceiverAddress  NOTIFY receiverAddressChanged)
    Q_PROPERTY(QString  available          READ getAvailable                                    NOTIFY availableChanged)
    Q_PROPERTY(QString  missing            READ getMissing                                      NOTIFY availableChanged)
    Q_PROPERTY(bool     isEnough           READ isEnough                                        NOTIFY availableChanged)
    Q_PROPERTY(QString  change             READ getChange                                       NOTIFY availableChanged)
    Q_PROPERTY(int      minimumFeeInGroth  READ getMinFeeInGroth      CONSTANT)
    Q_PROPERTY(int      defaultFeeInGroth  READ getDefaultFeeInGroth  CONSTANT)

public:
    SendViewModel();
    ~SendViewModel() override = default;

    int  getFeeGrothes() const;
    void setFeeGrothes(int amount);

    void setComment(const QString& value);
    QString getComment() const;

    double getSendAmount() const;
    void setSendAmount(double value);

    QString getReceiverAddress() const;
    void setReceiverAddress(const QString& value);

    int getMinFeeInGroth() const;
    int getDefaultFeeInGroth() const;

    QString getAvailable() const;
    QString getMissing() const;
    bool isEnough() const;
    QString getChange() const;

public:
    Q_INVOKABLE bool isValidReceiverAddress(const QString& value);
    Q_INVOKABLE void sendMoney();
    Q_INVOKABLE bool needPassword() const;
    Q_INVOKABLE bool isPasswordValid(const QString& value) const;

signals:
    void feeGrothesChanged();
    void commentChanged();
    void sendAmountChanged();
    void receiverAddressChanged();
    void availableChanged();
    void sendMoneyVerified();
    void cantSendToExpired();

public slots:
    void onChangeCalculated(beam::Amount change);
    void onChangeWalletIDs(beam::wallet::WalletID senderID, beam::wallet::WalletID receiverID);
    void onSendMoneyVerified();
    void onCantSendToExpired();

private:
    beam::Amount calcTotalAmount() const;
    beam::Amount calcSendAmount() const;
    beam::Amount calcFeeAmount() const;

    int _feeGrothes;
    double _sendAmount;
    QString _comment;
    QString _receiverAddr;
    StatusHolder _status;
    beam::Amount _change;
    WalletModel& _walletModel;
};
