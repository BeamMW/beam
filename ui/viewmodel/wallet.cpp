#include "wallet.h"

#include <QDateTime>

#include <iomanip>

using namespace beam;
using namespace std;

namespace
{

	QString BeamToString(const Amount& value)
	{
		auto str = std::to_string(double(int64_t(value)) / Rules::Coin);

		str.erase(str.find_last_not_of('0') + 1, std::string::npos);
		str.erase(str.find_last_not_of('.') + 1, std::string::npos);

		return QString::fromStdString(str);
	}

	inline void ltrim(std::string &s, char sym) 
	{
    	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [sym](char ch) {return ch != sym;}));
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
	auto id = std::to_string(_tx.m_peerId);
	ltrim(id, '0');
	return QString::fromStdString(id);
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
	, _status{ 0, 0, 0, 0, {0, 0, 0} }
	, _sendAmount("0")
	, _sendAmountMils("0")
    , _feeMils("0")
    , _change(0)
    , _keychain(keychain)
{
	connect(&_model, SIGNAL(onStatus(const WalletStatus&)), SLOT(onStatus(const WalletStatus&)));

	connect(&_model, SIGNAL(onTxStatus(const std::vector<beam::TxDescription>&)), 
		SLOT(onTxStatus(const std::vector<beam::TxDescription>&)));

	connect(&_model, SIGNAL(onTxPeerUpdated(const std::vector<beam::TxPeer>&)),
		SLOT(onTxPeerUpdated(const std::vector<beam::TxPeer>&)));

	connect(&_model, SIGNAL(onSyncProgressUpdated(int, int)),
		SLOT(onSyncProgressUpdated(int, int)));

    connect(&_model, SIGNAL(onChangeCalculated(beam::Amount)),
        SLOT(onChangeCalculated(beam::Amount)));

	_model.start();
}

void WalletViewModel::onStatus(const WalletStatus& status)
{
	bool changed = false;

	if (_status.available != status.available)
	{
		_status.available = status.available;

		changed = true;

		emit actualAvailableChanged();
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

	if (_status.update.lastTime != status.update.lastTime)
	{
		_status.update.lastTime = status.update.lastTime;

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

void WalletViewModel::onSyncProgressUpdated(int done, int total)
{
	_status.update.done = done;
	_status.update.total = total;

	emit stateChanged();
}

void WalletViewModel::onChangeCalculated(beam::Amount change)
{
    _change = change;
    emit actualAvailableChanged();
    emit changeChanged();
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

QString WalletViewModel::feeMils() const
{
    return _feeMils;
}

void WalletViewModel::setSendAmount(const QString& amount)
{
	if (amount != _sendAmount)
	{
		_sendAmount = amount;
        _model.async->calcChange(calcTotalAmount());
		emit sendAmountChanged();
		emit actualAvailableChanged();
	}
}

void WalletViewModel::setSendAmountMils(const QString& amount)
{
	if (amount != _sendAmountMils)
	{
		_sendAmountMils = amount;
        _model.async->calcChange(calcTotalAmount());
		emit sendAmountMilsChanged();
		emit actualAvailableChanged();
	}
}

void WalletViewModel::setFeeMils(const QString& amount)
{
    if (amount != _feeMils)
    {
        _feeMils = amount;
        _model.async->calcChange(calcTotalAmount());
        emit feeMilsChanged();
        emit actualAvailableChanged();
    }
}

void WalletViewModel::setSelectedAddr(int index)
{
	_selectedAddr = index;
	emit selectedAddrChanged();
}

QString WalletViewModel::receiverAddr() const
{
	if (_selectedAddr < 0 || _addrList.empty()) return "";

	stringstream str;
	str << _addrList[_selectedAddr].m_walletID;
	return QString::fromStdString(str.str());
}

QVariant WalletViewModel::tx() const
{
	return QVariant::fromValue(_tx);
}

QVariant WalletViewModel::addrBook() const
{
	QStringList book;

	for (const auto& item : _addrList)
	{
		book.append(QString::fromStdString(item.m_label));
	}

	return QVariant::fromValue(book);
}

QString WalletViewModel::syncTime() const
{
	auto time = beam::format_timestamp("%Y.%m.%d %H:%M:%S", _status.update.lastTime * 1000, false);

	return QString::fromStdString(time);
}

int WalletViewModel::syncProgress() const
{
	if (_status.update.total > 0)
	{
		return _status.update.done * 100 / _status.update.total;
	}

	return -1;
}

int WalletViewModel::selectedAddr() const
{
	return _selectedAddr;
}

beam::Amount WalletViewModel::calcSendAmount() const
{
	return _sendAmount.toInt() * Rules::Coin + _sendAmountMils.toInt();
}

beam::Amount WalletViewModel::calcFeeAmount() const
{
    return _feeMils.toInt();
}

beam::Amount WalletViewModel::calcTotalAmount() const
{
    return calcSendAmount() + calcFeeAmount();
}


void WalletViewModel::sendMoney()
{
    if (_selectedAddr > -1)
    {
        auto& addr = _addrList[_selectedAddr];
        // TODO: show 'operation in process' animation here?

        _model.async->sendMoney(addr.m_walletID, calcSendAmount(), calcFeeAmount());
    }
}

void WalletViewModel::syncWithNode()
{
	_model.async->syncWithNode();
}

QString WalletViewModel::actualAvailable() const
{
	return BeamToString(_status.available - calcTotalAmount() - _change);
}

QString WalletViewModel::change() const
{
    return BeamToString(_change);
}
