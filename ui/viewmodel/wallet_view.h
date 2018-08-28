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

class TxObject : public QObject
{
    Q_OBJECT

        Q_PROPERTY(bool income           READ income       NOTIFY incomeChanged)
        Q_PROPERTY(QString date          READ date         NOTIFY dateChanged)
        Q_PROPERTY(QString user          READ user         NOTIFY userChanged)
        Q_PROPERTY(QString userName      READ userName     NOTIFY userChanged)
		Q_PROPERTY(QString displayName   READ displayName  NOTIFY displayNameChanged)
        Q_PROPERTY(QString comment       READ comment      NOTIFY commentChanged)
        Q_PROPERTY(QString amount        READ amount       NOTIFY amountChanged)
        Q_PROPERTY(QString change        READ change       NOTIFY changeChanged)
        Q_PROPERTY(QString status        READ status       NOTIFY statusChanged)
        Q_PROPERTY(bool canCancel        READ canCancel    NOTIFY canCancelChanged)
public:

    TxObject() = default;
    TxObject(const beam::TxDescription& tx);

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

	beam::WalletID peerId() const;

	void setUserName(QString name);
    void setDisplayName(QString name);

signals:
    void incomeChanged();
    void dateChanged();
    void userChanged();
	void displayNameChanged();
    void commentChanged();
    void amountChanged();
    void changeChanged();
    void statusChanged();
    void canCancelChanged();

public:
    beam::TxDescription _tx;
	QString _userName;
    QString _displayName;
};

class WalletViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString available     READ available       NOTIFY stateChanged)
    Q_PROPERTY(QString received      READ received        NOTIFY stateChanged)
    Q_PROPERTY(QString sent          READ sent            NOTIFY stateChanged)
    Q_PROPERTY(QString unconfirmed   READ unconfirmed     NOTIFY stateChanged)

    Q_PROPERTY(QString sendAmount READ sendAmount WRITE setSendAmount NOTIFY sendAmountChanged)

    Q_PROPERTY(QString feeMils READ feeMils WRITE setFeeMils NOTIFY feeMilsChanged)

    Q_PROPERTY(QString receiverAddr READ getReceiverAddr WRITE setReceiverAddr NOTIFY receiverAddrChanged)
    //Q_PROPERTY(QString senderAddr READ getSenderAddr WRITE setSenderAddr NOTIFY senderAddrChanged)
    Q_PROPERTY(QQmlListProperty<TxObject> tx READ tx NOTIFY txChanged)
    Q_PROPERTY(int selectedAddr READ selectedAddr WRITE setSelectedAddr NOTIFY selectedAddrChanged)

    Q_PROPERTY(QString syncTime READ syncTime NOTIFY stateChanged)
    Q_PROPERTY(int syncProgress READ syncProgress NOTIFY stateChanged)
    Q_PROPERTY(bool isSyncInProgress READ getIsSyncInProgress WRITE setIsSyncInProgress NOTIFY isSyncInProgressChanged)

    Q_PROPERTY(QString actualAvailable READ actualAvailable NOTIFY actualAvailableChanged)
    Q_PROPERTY(QString change READ change NOTIFY changeChanged)

    Q_PROPERTY(QString newReceiverAddr READ getNewReceiverAddr NOTIFY newReceiverAddrChanged)
    Q_PROPERTY(QString newReceiverName WRITE setNewReceiverName NOTIFY newReceiverNameChanged)

    Q_PROPERTY(QString comment WRITE setComment NOTIFY commentedChaned)

    
public:

    Q_INVOKABLE void cancelTx(int index);
    Q_INVOKABLE void generateNewAddress();
    Q_INVOKABLE void saveNewAddress();
    Q_INVOKABLE void copyToClipboard(const QString& text);

public:
    using TxList = QList<TxObject*>;
    
    WalletViewModel();
    virtual ~WalletViewModel();
    
    QString available() const;
    QString received() const;
    QString sent() const;
    QString unconfirmed() const;

    QQmlListProperty<TxObject> tx();
    QString sendAmount() const;
    QString feeMils() const;
    QString receiverAddr() const;
    QString syncTime() const;
    int syncProgress() const;
    bool getIsSyncInProgress() const;
    void setIsSyncInProgress(bool value);

    QString actualAvailable() const;
    QString change() const;
    QString getNewReceiverAddr() const;
    void setNewReceiverName(const QString& value);
    int selectedAddr() const;
    
    QString getReceiverAddr() const;
    void setReceiverAddr(const QString& value);
    /*QString getSenderAddr() const;
    void setSenderAddr(const QString& value);*/

    void setSendAmount(const QString& text);
    void setSendAmountMils(const QString& text);
    void setFeeMils(const QString& text);
    void setSelectedAddr(int index);
    void setComment(const QString& value);

public slots:
    void onStatus(const WalletStatus& amount);
    void onTxStatus(const std::vector<beam::TxDescription>& history);
    void sendMoney();
    void syncWithNode();
    void onTxPeerUpdated(const std::vector<beam::TxPeer>& peers);
    void onSyncProgressUpdated(int done, int total);
    void onChangeCalculated(beam::Amount change);
    void onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID);
	void onAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses);
    void onGeneratedNewWalletID(const beam::WalletID& walletID);

signals:
    void stateChanged();

    void sendAmountChanged();
    void feeMilsChanged();
    void txChanged();
    void selectedAddrChanged();
    void actualAvailableChanged();
    void changeChanged();
    void receiverAddrChanged();
    void senderAddrChanged();
    void isSyncInProgressChanged();
    void newReceiverAddrChanged();
    void newReceiverNameChanged();
    void commentedChaned();

private:
    beam::Amount calcSendAmount() const;
    beam::Amount calcFeeAmount() const;
    beam::Amount calcTotalAmount() const;

private:

    WalletModel& _model;

    WalletStatus _status;

    QString _sendAmount;
    QString _feeMils;

    beam::Amount _change;

    TxList _tx;

    std::vector<beam::TxPeer> _addrList;

    QString _receiverAddr;
    //QString _senderAddr;
    QString _newReceiverAddr;
    QString _newReceiverName;
    QString _comment;

    int _selectedAddr;
    bool _isSyncInProgress;
};
