#include "wallet.h"

#include <QDateTime>
#include <QMessageBox>

using namespace beam;

namespace
{
	QString BeamToString(const Amount& value)
	{
		return QString::number(static_cast<float>(value) / Rules::Coin, 'f') + " BEAM";
	}
}

TxObject::TxObject(const TxDescription& tx) : _tx(tx) {}

QString TxObject::dir() const
{
	return _tx.m_sender ? "outcome" : "income";
}

QString TxObject::date() const
{
	QDateTime datetime;
	datetime.setTime_t(_tx.m_createTime);

	return datetime.toString(Qt::SystemLocaleShortDate);
}

QString TxObject::user() const
{
	return QString::number(_tx.m_peerId);
}

QString TxObject::comment() const
{
	return "";
}

QString TxObject::amount() const
{
	return QString(_tx.m_sender ? "- " : "+ ") +  BeamToString(_tx.m_amount);
}

QString TxObject::amountUsd() const
{
	// TODO: don't know how we're going to calc amount USD
	return QString::number(_tx.m_amount) + " USD";
}

QString TxObject::status() const
{
	static const char* Names[] = { "Pending", "InProgress", "Cancelled", "Completed", "Failed" };
	return Names[_tx.m_status];
}

WalletViewModel::WalletViewModel(IKeyChain::Ptr keychain)
	: _model(keychain)
	, _available(0)
	, _sendAmount(40)
	, _receiverAddr("127.0.0.1:8888")
{
	connect(&_model, SIGNAL(onStatus(const beam::Amount&)), SLOT(onStatus(const beam::Amount&)));
	connect(&_model, SIGNAL(onTxStatus(const std::vector<beam::TxDescription>&)), 
		SLOT(onTxStatus(const std::vector<beam::TxDescription>&)));

	_model.start();
}

void WalletViewModel::onStatus(const beam::Amount& amount)
{
	if (_available != amount)
	{
		_available = amount;

		emit availableChanged();
	}
}

void WalletViewModel::onTxStatus(const std::vector<TxDescription>& history)
{
	_tx.clear();

	for (const auto& item : history)
	{
		_tx.append(new TxObject(item));
	}

	emit txChanged();
}

QString WalletViewModel::available() const
{
	return BeamToString(_available);
}

QString WalletViewModel::sendAmount() const
{
	return QString::number(_sendAmount);
}

void WalletViewModel::setSendAmount(const QString& text)
{
	beam::Amount amount = text.toUInt();

	if (amount != _sendAmount)
	{
		_sendAmount = amount;
		emit sendAmountChanged();
	}
}

void WalletViewModel::setReceiverAddr(const QString& text)
{
	std::string addr = text.toStdString();

	if (addr != _receiverAddr)
	{
		_receiverAddr = addr;
		emit receiverAddrChanged();
	}
}

QString WalletViewModel::receiverAddr() const
{
	return QString(_receiverAddr.c_str());
}

QVariant WalletViewModel::tx() const
{
	return QVariant::fromValue(_tx);
}

void WalletViewModel::sendMoney()
{
	io::Address receiverAddr;
	
	if (receiverAddr.resolve(_receiverAddr.c_str()))
	{
		// TODO: show 'operation in process' animation here?
		_model.async->sendMoney(receiverAddr, std::move(_sendAmount));

	}
	else QMessageBox::critical(0, "Error", (std::string("unable to resolve receiver address: ") + _receiverAddr).c_str(), QMessageBox::Ok);
}
