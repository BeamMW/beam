#include "wallet.h"

#include <QDateTime>

using namespace beam;
using namespace std;

namespace
{
	QString BeamToString(const Amount& value, int prec = 3)
	{
		return QString::number(static_cast<float>(value) / Rules::Coin, 'f', prec);
	}
}

TxObject::TxObject(const TxDescription& tx) : _tx(tx) {}

bool TxObject::income() const
{
	return _tx.m_sender == false;
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
	return BeamToString(_tx.m_amount);
}

QString TxObject::amountUsd() const
{
	// TODO: don't know how we're going to calc amount USD
	return BeamToString(_tx.m_amount) + " USD";
}

QString TxObject::status() const
{
	static const char* Names[] = { "Pending", "InProgress", "Cancelled", "Completed", "Failed" };
	return Names[_tx.m_status];
}

WalletViewModel::WalletViewModel(IKeyChain::Ptr keychain, uint16_t port, const string& nodeAddr)
	: _model(keychain, port, nodeAddr)
	, _status{0, 0, 0, 0}
	, _sendAmount("40.000")
	, _receiverAddr("127.0.0.1:8888")
{
	connect(&_model, SIGNAL(onStatus(const WalletStatus&)), SLOT(onStatus(const WalletStatus&)));

	connect(&_model, SIGNAL(onTxStatus(const std::vector<beam::TxDescription>&)), 
		SLOT(onTxStatus(const std::vector<beam::TxDescription>&)));

	_model.start();
}

void WalletViewModel::onStatus(const WalletStatus& status)
{
	if (_status.available != status.available)
	{
		_status.available = status.available;

		emit availableChanged();
	}

	if (_status.received != status.received)
	{
		_status.received = status.received;

		emit receivedChanged();
	}

	if (_status.sent != status.sent)
	{
		_status.sent = status.sent;

		emit sentChanged();
	}

	if (_status.unconfirmed != status.unconfirmed)
	{
		_status.unconfirmed = status.unconfirmed;

		emit unconfirmedChanged();
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
	return BeamToString(_status.available);
}

QString WalletViewModel::received() const
{
	return BeamToString(_status.received);
}

QString WalletViewModel::sent() const
{
	return BeamToString(_status.sent);
}

QString WalletViewModel::unconfirmed() const
{
	return BeamToString(_status.unconfirmed);
}

QString WalletViewModel::sendAmount() const
{
	return _sendAmount;
}

void WalletViewModel::setSendAmount(const QString& amount)
{
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
		_model.async->sendMoney(std::move(receiverAddr), std::move(_sendAmount.toFloat() * Rules::Coin));

	}
	else
	{
		LOG_ERROR() << std::string("unable to resolve receiver address: ") + _receiverAddr;
	}
}
