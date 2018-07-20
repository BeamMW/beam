#include "wallet.h"

#include <QDateTime>

#include <iomanip>

using namespace beam;
using namespace std;

namespace
{

	QString BeamToString(const Amount& value)
	{
		auto str = std::to_string(value / Rules::Coin);

		int fraction = value % Rules::Coin;

		if (fraction)
		{
			std::stringstream fracStream;
			fracStream << std::setw(log10(Rules::Coin)) << std::setfill('0') << fraction;

			auto fracString = fracStream.str();
			fracString.erase(fracString.find_last_not_of('0') + 1, std::string::npos);

			str += "." + fracString;
		}

		return QString::fromStdString(str);
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
	return QString::fromStdString(std::to_string(_tx.m_peerId));
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


QString TxObject::change() const
{
	return BeamToString(_tx.m_change) + " BEAM";
}

QString TxObject::status() const
{
	static const char* Names[] = { "Pending", "InProgress", "Cancelled", "Completed", "Failed" };
	return Names[_tx.m_status];
}

WalletViewModel::WalletViewModel(IKeyChain::Ptr keychain, uint16_t port, const string& nodeAddr)
	: _model(keychain, port, nodeAddr)
	, _status{0, 0, 0, 0}
	, _sendAmount("0")
	, _sendAmountMils("0")
	, _receiverAddr("127.0.0.1:8888")
    , _keychain(keychain)
{
	connect(&_model, SIGNAL(onStatus(const WalletStatus&)), SLOT(onStatus(const WalletStatus&)));

	connect(&_model, SIGNAL(onTxStatus(const std::vector<beam::TxDescription>&)), 
		SLOT(onTxStatus(const std::vector<beam::TxDescription>&)));

	connect(&_model, SIGNAL(onTxPeerUpdated(const std::vector<beam::TxPeer>&)),
		SLOT(onTxPeerUpdated(const std::vector<beam::TxPeer>&)));

	_model.start();
}

void WalletViewModel::onStatus(const WalletStatus& status)
{
	bool changed = false;

	if (_status.available != status.available)
	{
		_status.available = status.available;

		changed = true;
	}

	if (_status.received != status.received)
	{
		_status.received = status.received;

		changed = true;
	}

	if (_status.sent != status.sent)
	{
		_status.sent = status.sent;

		changed = true;
	}

	if (_status.unconfirmed != status.unconfirmed)
	{
		_status.unconfirmed = status.unconfirmed;

		changed = true;
	}

	if (_status.lastUpdateTime != status.lastUpdateTime)
	{
		_status.lastUpdateTime = status.lastUpdateTime;

		changed = true;
	}

	if(changed)
		emit stateChanged();
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

void WalletViewModel::onTxPeerUpdated(const std::vector<beam::TxPeer>& peers)
{
	_addrList = peers;

	emit addrBookChanged();
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

QString WalletViewModel::sendAmountMils() const
{
	return _sendAmountMils;
}

void WalletViewModel::setSendAmount(const QString& amount)
{
	if (amount != _sendAmount)
	{
		_sendAmount = amount;
		emit sendAmountChanged();
	}
}

void WalletViewModel::setSendAmountMils(const QString& amount)
{
	if (amount != _sendAmountMils)
	{
		_sendAmountMils = amount;
		emit sendAmountMilsChanged();
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

void WalletViewModel::setSelectedAddr(int index)
{
	_selectedAddr = index;
	emit selectedAddrChanged();
}

QString WalletViewModel::receiverAddr() const
{
	return QString(_receiverAddr.c_str());
}

QVariant WalletViewModel::tx() const
{
	return QVariant::fromValue(_tx);
}

QVariant WalletViewModel::addrBook() const
{
	QStringList book;

	for (auto& const item : _addrList)
	{
		book.append(QString::fromStdString(item.m_label));
	}

	return QVariant::fromValue(book);
}

QString WalletViewModel::syncTime() const
{
	auto time = beam::format_timestamp("%Y.%m.%d %H:%M:%S", local_timestamp_msec(), false);

	return QString::fromStdString(time);
}

void WalletViewModel::sendMoney()
{
    if (_selectedAddr > -1)
    {
        auto& addr = _addrList[_selectedAddr];
        // TODO: show 'operation in process' animation here?

        _model.async->sendMoney(addr.m_walletID, std::move(_sendAmount.toInt() * Rules::Coin + _sendAmountMils.toInt()));
    }
    else
    {
        LOG_ERROR() << std::string("unable to resolve receiver address: ") + _receiverAddr;
    }
}
