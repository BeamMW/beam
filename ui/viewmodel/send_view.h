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

    // TA = Transaction or Address
    Q_PROPERTY(QString  receiverTA         READ getReceiverTA         WRITE setReceiverTA       NOTIFY receiverTAChanged)
    Q_PROPERTY(bool     receiverTAValid    READ getRreceiverTAValid                             NOTIFY receiverTAChanged)

    Q_PROPERTY(QString  receiverAddress    READ getReceiverAddress                              NOTIFY receiverTAChanged)
    Q_PROPERTY(double   available          READ getAvailable                                    NOTIFY availableChanged)
    Q_PROPERTY(double   change             READ getChange                                       NOTIFY availableChanged)
    Q_PROPERTY(double   totalUTXO          READ getTotalUTXO                                    NOTIFY availableChanged)
    Q_PROPERTY(double   missing            READ getMissing                                      NOTIFY availableChanged)
    Q_PROPERTY(bool     isEnough           READ isEnough                                        NOTIFY availableChanged)
    Q_PROPERTY(bool     canSend            READ canSend                                         NOTIFY canSendChanged)

public:
    SendViewModel();
    ~SendViewModel();

    int  getFeeGrothes() const;
    void setFeeGrothes(int amount);

    void setComment(const QString& value);
    QString getComment() const;

    double getSendAmount() const;
    void setSendAmount(double value);

    QString getReceiverTA() const;
    void    setReceiverTA(const QString& value);
    bool    getRreceiverTAValid() const;
    QString getReceiverAddress() const;

    double getAvailable() const;
    double getMissing() const;
    double getChange() const;
    double getTotalUTXO() const;

    bool isEnough() const;
    bool canSend() const;

public:
    Q_INVOKABLE void sendMoney();

signals:
    void feeGrothesChanged();
    void commentChanged();
    void sendAmountChanged();
    void receiverTAChanged();
    void availableChanged();
    void sendMoneyVerified();
    void cantSendToExpired();
    void canSendChanged();

public slots:
    void onChangeCalculated(beam::Amount change);
    void onSendMoneyVerified();
    void onCantSendToExpired();

private:
    beam::Amount calcTotalAmount() const;
    beam::Amount calcSendAmount() const;
    beam::Amount calcFeeAmount() const;
    void extractParameters();

    int     _feeGrothes;
    double  _sendAmount;
    QString _comment;
    QString _receiverTA;
    StatusHolder _status;
    beam::Amount _change;
    WalletModel& _walletModel;
    beam::wallet::TxParameters _txParameters;
};
