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

#include "address_book_view.h"
#include "ui_helpers.h"
#include <QApplication>
#include <QClipboard>
#include "model/app_model.h"

using namespace std;
using namespace beam;
using namespace beamui;

PeerAddressItem::PeerAddressItem()
    : m_walletID{}
    , m_name{}
    , m_category{}
{

}

PeerAddressItem::PeerAddressItem(const beam::WalletAddress& address)
    : m_walletID{beamui::toString(address.m_walletID)}
    , m_name{QString::fromStdString(address.m_label)}
    , m_category(QString::fromStdString(address.m_category))
{

}

QString PeerAddressItem::getWalletID() const
{
    return m_walletID;
}

void PeerAddressItem::setWalletID(const QString& value)
{
    m_walletID = value;
    emit onWalletIDChanged();
}

QString PeerAddressItem::getName() const
{
    return m_name;
}

void PeerAddressItem::setName(const QString& value)
{
    m_name = value;
    emit onNameChanged();
}

QString PeerAddressItem::getCategory() const
{
    return m_category;
}

void PeerAddressItem::setCategory(const QString& value)
{
    m_category = value;
    emit onCategoryChanged();
}

void PeerAddressItem::clean()
{
    setWalletID(QString{});
    setName(QString{});
    setCategory(QString{});
}

OwnAddressItem::OwnAddressItem()
    : PeerAddressItem{}
    , m_expirationDate{}
    , m_createDate{}
{

}

OwnAddressItem::OwnAddressItem(const beam::WalletAddress& address)
    : PeerAddressItem{ address }
    , m_expirationDate{toString(address.m_createTime + address.m_duration)}
    , m_createDate{ toString(address.m_createTime) }
{

}

void OwnAddressItem::setExpirationDate(const QString& value)
{
    m_expirationDate = value;
}

void OwnAddressItem::setCreateDate(const QString& value)
{
    m_createDate = value;
}

QString OwnAddressItem::getExpirationDate() const
{
    return m_expirationDate;
}

QString OwnAddressItem::getCreateDate() const
{
    return m_createDate;
}

void OwnAddressItem::clean()
{
    PeerAddressItem::clean();
    setExpirationDate(QString{});
    setCreateDate(QString{});
}

AddressBookViewModel::AddressBookViewModel()
    : m_model{*AppModel::getInstance()->getWallet()}
{
    connect(&m_model, SIGNAL(onStatus(const WalletStatus&)),
        SLOT(onStatus(const WalletStatus&)));

    connect(&m_model, SIGNAL(onAdrresses(bool, const std::vector<beam::WalletAddress>&)),
        SLOT(onAdrresses(bool, const std::vector<beam::WalletAddress>&)));

    connect(&m_model, SIGNAL(onGeneratedNewWalletID(const beam::WalletID&)),
        SLOT(onGeneratedNewWalletID(const beam::WalletID&)));

    getAddressesFromModel();
}

QQmlListProperty<PeerAddressItem> AddressBookViewModel::getPeerAddresses()
{
    return QQmlListProperty<PeerAddressItem>(this, m_peerAddresses);
}

QQmlListProperty<OwnAddressItem> AddressBookViewModel::getOwnAddresses()
{
    return QQmlListProperty<OwnAddressItem>(this, m_ownAddresses);
}

PeerAddressItem* AddressBookViewModel::getNewPeerAddress()
{
    return &m_newPeerAddress;
}

OwnAddressItem* AddressBookViewModel::getNewOwnAddress()
{
    return &m_newOwnAddress;
}

void AddressBookViewModel::generateNewEmptyAddress()
{
    m_newOwnAddress.clean();
    m_newPeerAddress.clean();

    m_model.getAsync()->generateNewWalletID();
}

void AddressBookViewModel::createNewPeerAddress()
{
    auto bytes = from_hex(m_newPeerAddress.getWalletID().toStdString());
    if (bytes.size() > sizeof(WalletID))
    {
        return;
    }
    WalletID walletID = bytes;
    WalletAddress peerAddress{};

    peerAddress.m_walletID = walletID;
    peerAddress.m_OwnID = 0;
    peerAddress.m_label = m_newPeerAddress.getName().toStdString();
    peerAddress.m_createTime = beam::getTimestamp();
    peerAddress.m_category = m_newPeerAddress.getCategory().toStdString();

    m_model.getAsync()->createNewAddress(std::move(peerAddress), false);
}

void AddressBookViewModel::createNewOwnAddress()
{
    auto bytes = from_hex(m_newOwnAddress.getWalletID().toStdString());
    if (bytes.size() != sizeof(WalletID))
    {
        return;
    }
    WalletID id = bytes;
    WalletAddress ownAddress{};

    ownAddress.m_walletID = id;
    ownAddress.m_OwnID = 0; // would be corrected
    ownAddress.m_label = m_newOwnAddress.getName().toStdString();
    ownAddress.m_createTime = beam::getTimestamp();
    // TODO implement expiration date and duration
    //ownAddress.m_duration = m_newOwnAddress.getExpirationDate
    ownAddress.m_category = m_newOwnAddress.getCategory().toStdString();

    m_model.getAsync()->createNewAddress(std::move(ownAddress), true);
}

void AddressBookViewModel::changeCurrentPeerAddress(int index)
{
    if (m_model.getAsync())
    {
        WalletID senderID = from_hex(m_ownAddresses.at(0)->getWalletID().toStdString());
        WalletID receivedID = from_hex(m_peerAddresses.at(index)->getWalletID().toStdString());

        m_model.getAsync()->changeCurrentWalletIDs(senderID, receivedID);
    }
}

void AddressBookViewModel::deletePeerAddress(int index)
{
    WalletID peerID = from_hex(m_peerAddresses.at(index)->getWalletID().toStdString());
    m_model.getAsync()->deleteAddress(peerID);
}

void AddressBookViewModel::deleteOwnAddress(int index)
{
    WalletID peerID = from_hex(m_ownAddresses.at(index)->getWalletID().toStdString());
    m_model.getAsync()->deleteOwnAddress(peerID);
}

void AddressBookViewModel::copyToClipboard(const QString& text)
{
    QApplication::clipboard()->setText(text);
}

void AddressBookViewModel::onStatus(const WalletStatus&)
{
    getAddressesFromModel();
}

void AddressBookViewModel::onAdrresses(bool own, const std::vector<WalletAddress>& addresses)
{
    if (own)
    {
        m_ownAddresses.clear();

        for (const auto& addr : addresses)
        {
            m_ownAddresses.push_back(new OwnAddressItem(addr));
        }
    }
    else
    {
        m_peerAddresses.clear();

        for (const auto& addr : addresses)
        {
            m_peerAddresses.push_back(new PeerAddressItem(addr));
        }
    }

    emit addressesChanged();
}

void AddressBookViewModel::onGeneratedNewWalletID(const beam::WalletID& walletID)
{
    m_newOwnAddress.setWalletID(toString(walletID));
}

void AddressBookViewModel::getAddressesFromModel()
{
    m_model.getAsync()->getAddresses(true);
    m_model.getAsync()->getAddresses(false);
}
