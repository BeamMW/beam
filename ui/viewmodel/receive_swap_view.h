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
#include "currencies.h"

class ReceiveSwapViewModel: public QObject
{
    Q_OBJECT
    Q_PROPERTY(double   amountToReceive    READ getAmountToReceive    WRITE  setAmountToReceive  NOTIFY  amountToReceiveChanged)
    Q_PROPERTY(double   amountSent         READ getAmountSent         WRITE  setAmountSent       NOTIFY  amountSentChanged)
    Q_PROPERTY(int      receiveFee         READ getReceiveFee         WRITE  setReceiveFee       NOTIFY  receiveFeeChanged)
    Q_PROPERTY(int      sentFee            READ getSentFee            WRITE  setSentFee          NOTIFY  sentFeeChanged)
    Q_PROPERTY(int      offerExpires       READ getOfferExpires       WRITE  setOfferExpires     NOTIFY  offerExpiresChanged)
    Q_PROPERTY(QString  addressComment     READ getAddressComment     WRITE  setAddressComment   NOTIFY  addressCommentChanged)
    Q_PROPERTY(QString  receiverAddress    READ getReceiverAddress                               NOTIFY  receiverAddressChanged)
    Q_PROPERTY(QString  transactionToken   READ getTransactionToken   WRITE  setTransactionToken NOTIFY  transactionTokenChanged)
    Q_PROPERTY(bool     commentValid       READ getCommentValid                                  NOTIFY  commentValidChanged)

    Q_PROPERTY(WalletCurrency::Currency  receiveCurrency    READ getReceiveCurrency    WRITE  setReceiveCurrency  NOTIFY  receiveCurrencyChanged)
    Q_PROPERTY(WalletCurrency::Currency  sentCurrency       READ getSentCurrency       WRITE  setSentCurrency     NOTIFY  sentCurrencyChanged)

public:
    ReceiveSwapViewModel();
    ~ReceiveSwapViewModel() override;

signals:
    void amountToReceiveChanged();
    void amountSentChanged();
    void receiveFeeChanged();
    void sentFeeChanged();
    void receiveCurrencyChanged();
    void sentCurrencyChanged();
    void offerExpiresChanged();
    void addressCommentChanged();
    void receiverAddressChanged();
    void transactionTokenChanged();
    void newAddressFailed();
    void commentValidChanged();

public:
    Q_INVOKABLE void generateNewAddress();
    Q_INVOKABLE void saveAddress();
    Q_INVOKABLE void startListen();
    Q_INVOKABLE void publishToken();

private:
    double getAmountToReceive() const;
    void   setAmountToReceive(double value);

    double getAmountSent() const;
    void   setAmountSent(double value);

    int  getReceiveFee() const;
    void setReceiveFee(int value);

    int  getSentFee() const;
    void setSentFee(int value);

    Currency  getReceiveCurrency() const;
    void setReceiveCurrency(Currency value);

    Currency  getSentCurrency() const;
    void setSentCurrency(Currency value);

    void setOfferExpires(int value);
    int  getOfferExpires() const;

    void setAddressComment(const QString& value);
    QString getAddressComment() const;

    QString getReceiverAddress() const;

    void setTransactionToken(const QString& value);
    QString getTransactionToken() const;

    bool getCommentValid() const;

    void updateTransactionToken();

private slots:
    void onGeneratedNewAddress(const beam::wallet::WalletAddress& walletAddr);
    void onNewAddressFailed();

private:
    double  _amountToReceive;
    double  _amountSent;
    int     _receiveFee;
    int     _sentFee;
    Currency  _receiveCurrency;
    Currency  _sentCurrency;
    int     _offerExpires;
    QString _addressComment;
    QString _token;
    beam::wallet::WalletAddress _receiverAddress;
    WalletModel& _walletModel;
    beam::wallet::TxParameters _txParameters;
};
