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

class SendViewModel: public QObject
{
    Q_OBJECT
    Q_PROPERTY(unsigned int  feeGrothes         READ getFeeGrothes         WRITE setFeeGrothes       NOTIFY feeGrothesChanged)
    Q_PROPERTY(QString       sendAmount         READ getSendAmount         WRITE setSendAmount       NOTIFY sendAmountChanged)
    Q_PROPERTY(QString       comment            READ getComment            WRITE setComment          NOTIFY commentChanged)

    // TA = Transaction or Address
    Q_PROPERTY(QString  receiverTA         READ getReceiverTA         WRITE setReceiverTA       NOTIFY receiverTAChanged)
    Q_PROPERTY(bool     receiverTAValid    READ getRreceiverTAValid                             NOTIFY receiverTAChanged)

    Q_PROPERTY(QString  receiverAddress    READ getReceiverAddress                              NOTIFY receiverTAChanged)
    Q_PROPERTY(QString  available          READ getAvailable                                    NOTIFY availableChanged)
    Q_PROPERTY(QString  change             READ getChange                                       NOTIFY availableChanged)
    Q_PROPERTY(QString  totalUTXO          READ getTotalUTXO                                    NOTIFY availableChanged)
    Q_PROPERTY(QString  missing            READ getMissing                                      NOTIFY availableChanged)
    Q_PROPERTY(bool     isEnough           READ isEnough                                        NOTIFY availableChanged)
    Q_PROPERTY(bool     canSend            READ canSend                                         NOTIFY canSendChanged)

public:
    SendViewModel();

    unsigned int getFeeGrothes() const;
    void setFeeGrothes(unsigned int amount);

    void setComment(const QString& value);
    QString getComment() const;

    QString getSendAmount() const;
    void setSendAmount(QString value);

    QString getReceiverTA() const;
    void    setReceiverTA(const QString& value);
    bool    getRreceiverTAValid() const;
    QString getReceiverAddress() const;

    QString getAvailable() const;
    QString getMissing() const;
    QString getChange() const;
    QString getTotalUTXO() const;

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

private:
    beam::Amount calcTotalAmount() const;
    void extractParameters();

    beam::Amount _feeGrothes;
    beam::Amount _sendAmountGrothes;
    beam::Amount _changeGrothes;

    QString _comment;
    QString _receiverTA;

    WalletModel& _walletModel;
    beam::wallet::TxParameters _txParameters;
};
