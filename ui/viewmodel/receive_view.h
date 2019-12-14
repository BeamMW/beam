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

class QR;
class ReceiveViewModel: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString  amountToReceive    READ getAmountToReceive    WRITE  setAmountToReceive  NOTIFY  amountReceiveChanged)
    Q_PROPERTY(int      addressExpires     READ getAddressExpires     WRITE  setAddressExpires   NOTIFY  addressExpiresChanged)
    Q_PROPERTY(QString  addressComment     READ getAddressComment     WRITE  setAddressComment   NOTIFY  addressCommentChanged)
    Q_PROPERTY(QString  receiverAddress    READ getReceiverAddress                               NOTIFY  receiverAddressChanged)
    Q_PROPERTY(QString  receiverAddressQR  READ getReceiverAddressQR                             NOTIFY  receiverAddressChanged)
    Q_PROPERTY(QString  transactionToken   READ getTransactionToken   WRITE  setTranasctionToken NOTIFY  transactionTokenChanged)
    Q_PROPERTY(bool     commentValid       READ getCommentValid                                  NOTIFY  commentValidChanged)

public:
    ReceiveViewModel();
    ~ReceiveViewModel() override;

signals:
    void amountReceiveChanged();
    void addressExpiresChanged();
    void receiverAddressChanged();
    void addressCommentChanged();
    void transactionTokenChanged();
    void newAddressFailed();
    void commentValidChanged();

public:
    Q_INVOKABLE void generateNewAddress();
    Q_INVOKABLE void saveAddress();

private:
    QString getAmountToReceive() const;
    void    setAmountToReceive(QString value);

    void setAddressExpires(int value);
    int  getAddressExpires() const;

    QString getReceiverAddress() const;
    QString getReceiverAddressQR() const;

    void setAddressComment(const QString& value);
    QString getAddressComment() const;

    void setTranasctionToken(const QString& value);
    QString getTransactionToken() const;

    bool getCommentValid() const;

    void updateTransactionToken();

private slots:
    void onGeneratedNewAddress(const beam::wallet::WalletAddress& walletAddr);
    void onReceiverQRChanged();

private:
    beam::Amount _amountToReceiveGrothes;
    int          _addressExpires;
    QString      _addressComment;
    QString      _token;
    beam::wallet::WalletAddress _receiverAddress;

    std::unique_ptr<QR> _qr;
    WalletModel& _walletModel;
    beam::wallet::TxParameters _txParameters;
};
