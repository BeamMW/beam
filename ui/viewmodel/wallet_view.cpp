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

#include "wallet_view.h"

#include <iomanip>
#include "ui_helpers.h"
#include <QApplication>
#include <QClipboard>
#include "model/app_model.h"

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

QString TxObject::userName() const
{
    return _userName;
}

QString TxObject::displayName() const
{
    return _displayName;
}

QString TxObject::comment() const
{
    string str{ _tx.m_message.begin(), _tx.m_message.end() };

    return QString(str.c_str()).trimmed();
}

QString TxObject::amount() const
{
    return BeamToString(_tx.m_amount);
}

QString TxObject::change() const
{
    if (_tx.m_change)
    {
        return BeamToString(_tx.m_change);
    }
    return QString{};
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

void TxObject::setUserName(QString name)
{
    _userName = name;
}

void TxObject::setDisplayName(QString name)
{
    _displayName = name;
}

beam::WalletID TxObject::peerId() const
{
    return _tx.m_peerId;
}

WalletViewModel::WalletViewModel()
    : _model(*AppModel::getInstance()->getWallet())
    , _status{ 0, 0, 0, 0, {0, 0, 0}, {} }
    , _sendAmount("0")
    , _feeMils("0")
    , _change(0)
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

    connect(&_model, SIGNAL(onChangeCurrentWalletIDs(beam::WalletID, beam::WalletID)),
        SLOT(onChangeCurrentWalletIDs(beam::WalletID, beam::WalletID)));

    connect(&_model, SIGNAL(onAdrresses(bool, const std::vector<beam::WalletAddress>&)),
        SLOT(onAdrresses(bool, const std::vector<beam::WalletAddress>&)));

    connect(&_model, SIGNAL(onGeneratedNewWalletID(const beam::WalletID&)),
        SLOT(onGeneratedNewWalletID(const beam::WalletID&)));
    _model.async->getWalletStatus();
}

WalletViewModel::~WalletViewModel()
{

}

void WalletViewModel::cancelTx(int index)
{
    auto *p = static_cast<TxObject*>(_tx[index]);
    _model.async->cancelTx(p->_tx.m_txId);
}

void WalletViewModel::generateNewAddress()
{
    _newReceiverAddr = "";
    _newReceiverName = "";
    if (_model.async)
    {
        _model.async->generateNewWalletID();
    }
}

void WalletViewModel::saveNewAddress()
{
    auto bytes = from_hex(_newReceiverAddr.toStdString());
    if (bytes.size() != sizeof(WalletID))
    {
        return;
    }
    WalletID id = bytes;
    WalletAddress ownAddress{};

    ownAddress.m_walletID = id;
    ownAddress.m_own = true;
    ownAddress.m_label = _newReceiverName.toStdString();
    ownAddress.m_createTime = beam::getTimestamp();

    if (_model.async)
    {
        _model.async->createNewAddress(std::move(ownAddress));
    }
}

void WalletViewModel::copyToClipboard(const QString& text)
{
    QApplication::clipboard()->setText(text);
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

    // Get info for TxObject::_user_name (get wallets labels)
    if (_model.async)
    {
        _model.async->getAddresses(false);
    }
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

void WalletViewModel::onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID)
{
    //setSenderAddr(toString(senderID));
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

/*QString WalletViewModel::getSenderAddr() const
{
    return _senderAddr;
}

void WalletViewModel::setSenderAddr(const QString& value)
{
    _senderAddr = value;
    emit senderAddrChanged();
}*/

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

void WalletViewModel::setComment(const QString& value)
{
    _comment = value;
	emit commentChanged();
}

QString WalletViewModel::getComment() const
{
	return _comment;
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
    return toString(_status.update.lastTime);
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



beam::Amount WalletViewModel::calcSendAmount() const
{
	return _sendAmount.toDouble() * Rules::Coin;
}

beam::Amount WalletViewModel::calcFeeAmount() const
{
    return _feeMils.toDouble() * Rules::Coin;
}

beam::Amount WalletViewModel::calcTotalAmount() const
{
    return calcSendAmount() + calcFeeAmount();
}

void WalletViewModel::sendMoney()
{
    if (/*!_senderAddr.isEmpty() && */!_receiverAddr.isEmpty())
    {
        //WalletID ownAddr = from_hex(getSenderAddr().toStdString());
        WalletID peerAddr = from_hex(getReceiverAddr().toStdString());
        // TODO: show 'operation in process' animation here?
        //_model.async->sendMoney(ownAddr, peerAddr, calcSendAmount(), calcFeeAmount());
        _model.async->sendMoney(peerAddr, _comment.toStdString(), calcSendAmount(), calcFeeAmount());
    }
}

void WalletViewModel::syncWithNode()
{
    //setIsSyncInProgress(true);
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

QString WalletViewModel::getNewReceiverAddr() const
{
    return _newReceiverAddr;
}

void WalletViewModel::setNewReceiverName(const QString& value)
{
    _newReceiverName = value;
    emit newReceiverNameChanged();
}

QString WalletViewModel::getNewReceiverName() const
{
	return _newReceiverName;
}

void WalletViewModel::onAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses)
{
    if (own)
    {
        return;
    }

    for (auto* tx : _tx)
    {
        auto foundIter = std::find_if(addresses.cbegin(), addresses.cend(), 
                                      [tx](const auto& address) { return address.m_walletID == tx->peerId(); });

        if (foundIter != addresses.cend())
        {
            tx->setUserName(QString::fromStdString(foundIter->m_label));
        }
        else if (!tx->userName().isEmpty())
        {
            tx->setUserName(QString{});
        }

        auto displayName = tx->userName().isEmpty() ? tx->user() : tx->userName();
        tx->setDisplayName(displayName);
    }

    emit txChanged();
}

void WalletViewModel::onGeneratedNewWalletID(const beam::WalletID& walletID)
{
    _newReceiverAddr = toString(walletID);
    emit newReceiverAddrChanged();
}