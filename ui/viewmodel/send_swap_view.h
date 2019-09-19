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
#include "model/wallet_model.h"
#include "currencies.h"
#include "status_holder.h"

class SendSwapViewModel: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString    token            READ getToken            WRITE setToken       NOTIFY tokenChanged)
    Q_PROPERTY(bool       tokenValid       READ getTokenValid                            NOTIFY tokenChanged)
    Q_PROPERTY(bool       parametersValid  READ getParametersValid                       NOTIFY parametersChanged)
    Q_PROPERTY(double     sendAmount       READ getSendAmount                            NOTIFY sendAmountChanged)
    Q_PROPERTY(int        sendFee          READ getSendFee          WRITE setSendFee     NOTIFY sendFeeChanged)
    Q_PROPERTY(double     receiveAmount    READ getReceiveAmount                         NOTIFY receiveAmountChanged)
    Q_PROPERTY(int        receiveFee       READ getReceiveFee       WRITE setReceiveFee  NOTIFY receiveFeeChanged)
    Q_PROPERTY(QDateTime  offeredTime      READ getOfferedTime                           NOTIFY offeredTimeChanged)
    Q_PROPERTY(QDateTime  expiresTime      READ getExpiresTime                           NOTIFY expiresTimeChanged)
    Q_PROPERTY(bool       isEnough         READ isEnough                                 NOTIFY enoughChanged)
    Q_PROPERTY(bool       canSend          READ canSend                                  NOTIFY canSendChanged)
    Q_PROPERTY(QString    comment          READ getComment          WRITE setComment     NOTIFY commentChanged)
    Q_PROPERTY(QString    receiverAddress  READ getReceiverAddress                       NOTIFY tokenChanged)

    Q_PROPERTY(WalletCurrency::Currency  receiveCurrency  READ getReceiveCurrency  NOTIFY  receiveCurrencyChanged)
    Q_PROPERTY(WalletCurrency::Currency  sendCurrency     READ getSendCurrency     NOTIFY  sendCurrencyChanged)

public:
    SendSwapViewModel();
    ~SendSwapViewModel() override;

    QString getToken() const;
    void setToken(const QString& value);
    bool getTokenValid() const;

    bool getParametersValid() const;

    double getSendAmount() const;
    void setSendAmount(double value);

    int  getSendFee() const;
    void setSendFee(int amount);

    Currency getSendCurrency() const;
    void setSendCurrency(Currency value);

    double getReceiveAmount() const;
    void setReceiveAmount(double value);

    int  getReceiveFee() const;
    void setReceiveFee(int amount);

    Currency getReceiveCurrency() const;
    void setReceiveCurrency(Currency value);

    void setComment(const QString& value);
    QString getComment() const;

    QDateTime getOfferedTime() const;
    void setOfferedTime(const QDateTime& time);

    QDateTime getExpiresTime() const;
    void setExpiresTime(const QDateTime& time);

    bool isEnough() const;
    bool canSend() const;

    QString getReceiverAddress() const;

public:
    Q_INVOKABLE void setParameters(QVariant parameters);    /// used to pass TxParameters directly without Token generation
    Q_INVOKABLE void sendMoney();

signals:
    void tokenChanged();
    void parametersChanged();
    void canSendChanged();
    void sendCurrencyChanged();
    void receiveCurrencyChanged();
    void sendAmountChanged();
    void receiveAmountChanged();
    void sendFeeChanged();
    void receiveFeeChanged();
    void commentChanged();
    void offeredTimeChanged();
    void expiresTimeChanged();
    void enoughChanged();

public slots:
    void onChangeCalculated(beam::Amount change);

private:
    void fillParameters(beam::wallet::TxParameters parameters);
    void recalcAvailable();

    double    _sendAmount;
    int       _sendFee;
    Currency  _sendCurrency;
    double    _receiveAmount;
    int       _receiveFee;
    Currency  _receiveCurrency;
    double    _change;
    QDateTime _offeredTime;
    QDateTime _expiresTime;
    QString   _comment;
    QString   _token;

    StatusHolder _status;
    WalletModel& _walletModel;
    beam::wallet::TxParameters _txParameters;
};
