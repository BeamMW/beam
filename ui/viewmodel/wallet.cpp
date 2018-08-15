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

#include "wallet.h"

#include <iomanip>
#include "ui_helpers.h"

using namespace beam;
using namespace std;
using namespace beamui;

TxObject::TxObject(const TxDescription& tx) : _tx(tx) {}

bool TxObject::income() const
{
	return _tx.m_sender == false;
}

QString TxObject::date() const
{
    return toString(_tx.m_createTime);
}

QString TxObject::user() const
{
	return toString(_tx.m_peerId);
}

QString TxObject::comment() const
{
	return "";
}

QString TxObject::amount() const
{
	return BeamToString(_tx.m_amount);
}

QString TxObject::change() const
{
	return BeamToString(_tx.m_change) + " BEAM";
}

QString TxObject::status() const
{
	static const char* Names[] = { "Pending", "In Progress", "Cancelled", "Completed", "Failed" };
	return Names[_tx.m_status];
}

bool TxObject::canCancel() const
{
    return _tx.m_status == beam::TxDescription::InProgress
        || _tx.m_status == beam::TxDescription::Pending;
}


UtxoItem::UtxoItem(const beam::Coin& coin)
    : _coin{coin}
{

}

QString UtxoItem::amount() const
{
    return BeamToString(_coin.m_amount) + " BEAM";
}

QString UtxoItem::height() const
{
    return QString::number(_coin.m_createHeight);
}

QString UtxoItem::maturity() const
{
    if (_coin.m_maturity == static_cast<Height>(-1))
        return QString{"-"};
    return QString::number(_coin.m_maturity);
}

QString UtxoItem::status() const
{
    static const char* Names[] = 
    {
        "Unconfirmed",
        "Unspent",
        "Locked",
        "Spent"
    };
    return Names[_coin.m_status];
}

QString UtxoItem::type() const
{
    static const char* Names[] = 
    {
        "Comission",
        "Coinbase",
        "Kernel",
        "Regular",
        "Identity",
        "SChannelNonce"
    };
    return Names[static_cast<int>(_coin.m_key_type)];
}

WalletViewModel::WalletViewModel(WalletModel& model)
	: _model(model)
	, _status{ 0, 0, 0, 0, {0, 0, 0} }
	, _sendAmount("0")
	, _sendAmountMils("0")
    , _feeMils("0")
    , _change(0)
    , _loadingUtxo{false}
	, _loadingAllUtxo{false}
    , _isSyncInProgress{false}
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

    connect(&_model, SIGNAL(onUtxoChanged(const std::vector<beam::Coin>&)),
        SLOT(onUtxoChanged(const std::vector<beam::Coin>&)));

	connect(&_model, SIGNAL(onAllUtxoChanged(const std::vector<beam::Coin>&)),
		SLOT(onAllUtxoChanged(const std::vector<beam::Coin>&)));

	connect(&_model, SIGNAL(onChangeCurrentWalletIDs(beam::WalletID, beam::WalletID)),
		SLOT(onChangeCurrentWalletIDs(beam::WalletID, beam::WalletID)));
}

WalletViewModel::~WalletViewModel()
{

}

void WalletViewModel::cancelTx(int index)
{
    auto *p = static_cast<TxObject*>(_tx[index]);
    _model.async->cancelTx(p->_tx.m_txId);
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

    if (changed)
    {
        _utxos.clear();
		_allUtxos.clear();
        emit stateChanged();
    }
}

void WalletViewModel::onTxStatus(const std::vector<TxDescription>& history)
{
	_tx.clear();

	for (const auto& item : history)
	{
		_tx.push_back(new TxObject(item));
	}

	emit txChanged();
}

void WalletViewModel::onTxPeerUpdated(const std::vector<beam::TxPeer>& peers)
{
	_addrList = peers;
}

void WalletViewModel::onSyncProgressUpdated(int done, int total)
{
	_status.update.done = done;
	_status.update.total = total;
    setIsSyncInProgress(!(done == total));
    
	emit stateChanged();
}

void WalletViewModel::onChangeCalculated(beam::Amount change)
{
    _change = change;
    emit actualAvailableChanged();
    emit changeChanged();
}

void WalletViewModel::onUtxoChanged(const std::vector<beam::Coin>& utxos)
{
    _utxos.clear();

    for (const auto& utxo : utxos)
    {
        _utxos.push_back(new UtxoItem(utxo));
    }
    _loadingUtxo = false;

    emit utxoChanged();
}

void WalletViewModel::onAllUtxoChanged(const std::vector<beam::Coin>& utxos)
{
	_allUtxos.clear();

    std::vector<beam::Coin> tmp(utxos);

    std::sort(tmp.begin(), tmp.end(), [](const Coin& lf, const Coin& rt)
    {
        return lf.m_createHeight > rt.m_createHeight;
    });

	for (const auto& utxo : tmp)
	{
		_allUtxos.push_back(new UtxoItem(utxo));
	}
	_loadingAllUtxo = false;

	emit allUtxoChanged();
}

void WalletViewModel::onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID)
{
	setSenderAddr(toString(senderID));
	setReceiverAddr(toString(receiverID));
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

QString WalletViewModel::getReceiverAddr() const
{
    return _receiverAddr;
}

void WalletViewModel::setReceiverAddr(const QString& value)
{
    _receiverAddr = value;
	emit receiverAddrChanged();
}

QString WalletViewModel::getSenderAddr() const
{
    return _senderAddr;
}

void WalletViewModel::setSenderAddr(const QString& value)
{
    _senderAddr = value;
	emit senderAddrChanged();
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

QQmlListProperty<TxObject> WalletViewModel::tx()
{
	return QQmlListProperty<TxObject>(this, _tx);
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

bool WalletViewModel::getIsSyncInProgress() const
{
    return _isSyncInProgress;
}

void WalletViewModel::setIsSyncInProgress(bool value)
{
    if (_isSyncInProgress != value)
    {
        _isSyncInProgress = value;
        emit isSyncInProgressChanged();
    }
}

int WalletViewModel::selectedAddr() const
{
	return _selectedAddr;
}

QQmlListProperty<UtxoItem> WalletViewModel::utxos()
{
    if (_utxos.empty() && _loadingUtxo == false && _model.async)
    {
        _loadingUtxo = true;
        _model.async->getAvaliableUtxos();
    }
	return QQmlListProperty<UtxoItem>(this, _utxos);
}

QQmlListProperty<UtxoItem> WalletViewModel::allUtxos()
{
	if (_allUtxos.empty() && _loadingAllUtxo == false && _model.async)
	{
		_loadingAllUtxo = true;
		_model.async->getAllUtxos();
	}
	return QQmlListProperty<UtxoItem>(this, _allUtxos);
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
    if (!_senderAddr.isEmpty() && !_receiverAddr.isEmpty())
    {
        WalletID ownAddr = from_hex(getSenderAddr().toStdString());
        WalletID peerAddr = from_hex(getReceiverAddr().toStdString());
        // TODO: show 'operation in process' animation here?
        _model.async->sendMoney(ownAddr, peerAddr, calcSendAmount(), calcFeeAmount());
    }
}

void WalletViewModel::syncWithNode()
{
    setIsSyncInProgress(true);
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
