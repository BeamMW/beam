#pragma once

#include <QObject>
#include <QtCore/qvariant.h>

#include "model/wallet.h"

using AddrList = std::vector<beam::TxPeer>;

class TxObject : public QObject
{
	Q_OBJECT

		Q_PROPERTY(bool income			READ income		NOTIFY incomeChanged)
		Q_PROPERTY(QString date			READ date		NOTIFY dateChanged)
		Q_PROPERTY(QString user			READ user		NOTIFY userChanged)
		Q_PROPERTY(QString comment		READ comment	NOTIFY commentChanged)
		Q_PROPERTY(QString amount		READ amount		NOTIFY amountChanged)
		Q_PROPERTY(QString amountUsd	READ amountUsd	NOTIFY amountUsdChanged)
		Q_PROPERTY(QString change		READ change		NOTIFY changeChanged)
		Q_PROPERTY(QString status		READ status		NOTIFY statusChanged)

public:

	TxObject(const beam::TxDescription& tx);

	bool income() const;
	QString date() const;
	QString user() const;
	QString comment() const;
	QString amount() const;
	QString amountUsd() const;
	QString change() const;
	QString status() const;

signals:
	void incomeChanged();
	void dateChanged();
	void userChanged();
	void commentChanged();
	void amountChanged();
	void amountUsdChanged();
	void changeChanged();
	void statusChanged();

public:
	beam::TxDescription _tx;
};

class WalletViewModel : public QObject
{
	Q_OBJECT

	Q_PROPERTY(QString available 	READ available 		NOTIFY stateChanged)
	Q_PROPERTY(QString received 	READ received 		NOTIFY stateChanged)
	Q_PROPERTY(QString sent 		READ sent 			NOTIFY stateChanged)
	Q_PROPERTY(QString unconfirmed 	READ unconfirmed 	NOTIFY stateChanged)

	Q_PROPERTY(QString sendAmount READ sendAmount WRITE setSendAmount NOTIFY sendAmountChanged)
	Q_PROPERTY(QString sendAmountMils READ sendAmountMils WRITE setSendAmountMils NOTIFY sendAmountMilsChanged)
	Q_PROPERTY(QString receiverAddr READ receiverAddr WRITE setReceiverAddr NOTIFY receiverAddrChanged)
	Q_PROPERTY(QVariant tx READ tx NOTIFY txChanged)
	Q_PROPERTY(QVariant addrBook READ addrBook NOTIFY addrBookChanged)
    Q_PROPERTY(int selectedAddr WRITE setSelectedAddr NOTIFY selectedAddrChanged)

	Q_PROPERTY(QString syncTime READ syncTime NOTIFY stateChanged)
	Q_PROPERTY(int syncProgress READ syncProgress NOTIFY stateChanged)

	Q_PROPERTY(QString actualAvailable READ actualAvailable NOTIFY actualAvailableChanged)

public:
	using TxList = QList<QObject*>;

	WalletViewModel(beam::IKeyChain::Ptr keychain, uint16_t port, const std::string& nodeAddr);

	QString available() const;
	QString received() const;
	QString sent() const;
	QString unconfirmed() const;

	QVariant tx() const;
	QString sendAmount() const;
	QString sendAmountMils() const;
	QString receiverAddr() const;
	QVariant addrBook() const;
	QString syncTime() const;
	int syncProgress() const;
	QString actualAvailable() const;

	void setSendAmount(const QString& text);
	void setSendAmountMils(const QString& text);
	void setReceiverAddr(const QString& text);
	void setSelectedAddr(int index);

public slots:
	void onStatus(const WalletStatus& amount);
	void onTxStatus(const std::vector<beam::TxDescription>& history);
	void sendMoney();
	void syncWithNode();
	void onTxPeerUpdated(const std::vector<beam::TxPeer>& peers);
	void onSyncProgressUpdated(int done, int total);

signals:
	void stateChanged();

	void sendAmountChanged();
	void sendAmountMilsChanged();
	void receiverAddrChanged();
	void txChanged();
	void addrBookChanged();
    void selectedAddrChanged();
	void actualAvailableChanged();

private:
	beam::Amount&& calcSendAmount() const;

private:

	WalletStatus _status;

	QString _sendAmount;
	QString _sendAmountMils;

	std::string _receiverAddr;
    beam::IKeyChain::Ptr _keychain;
	TxList _tx;

	WalletModel _model;
	std::vector<beam::TxPeer> _addrList;

    int _selectedAddr;
};
