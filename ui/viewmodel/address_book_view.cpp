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

AddressItem::AddressItem(const beam::WalletAddress& address)
    : m_address{ beamui::toString(address.m_walletID) }
    , m_name{ QString::fromStdString(address.m_label) }
    , m_category{ QString::fromStdString(address.m_category) }
    , m_createDate{ toString(address.m_createTime) }
    , m_expirationDate{ address.m_createTime + address.m_duration }
{

}

QString AddressItem::getAddress() const
{
    return m_address;
}

QString AddressItem::getName() const
{
    return m_name;
}

QString AddressItem::getCategory() const
{
    return m_category;
}

QString AddressItem::getExpirationDate() const
{
    return toString(m_expirationDate);
}

QString AddressItem::getCreateDate() const
{
    return m_createDate;
}

bool AddressItem::isExpired() const
{
    return getTimestamp() > m_expirationDate;
}

ContactItem::ContactItem(const beam::WalletAddress& address)
    : m_address{ beamui::toString(address.m_walletID) }
    , m_name{ QString::fromStdString(address.m_label) }
    , m_category{ QString::fromStdString(address.m_category) }
{

}

QString ContactItem::getAddress() const
{
    return m_address;
}

QString ContactItem::getName() const
{
    return m_name;
}

QString ContactItem::getCategory() const
{
    return m_category;
}

AddressBookViewModel::AddressBookViewModel()
    : m_model{*AppModel::getInstance()->getWallet()}
{
    connect(&m_model, SIGNAL(onStatus(const WalletStatus&)),
        SLOT(onStatus(const WalletStatus&)));

    connect(&m_model, SIGNAL(onAdrresses(bool, const std::vector<beam::WalletAddress>&)),
        SLOT(onAdrresses(bool, const std::vector<beam::WalletAddress>&)));

    getAddressesFromModel();
}

QQmlListProperty<ContactItem> AddressBookViewModel::getContacts()
{
    return QQmlListProperty<ContactItem>(this, m_contacts);
}

QQmlListProperty<AddressItem> AddressBookViewModel::getActiveAddresses()
{
    return QQmlListProperty<AddressItem>(this, m_activeAddresses);
}

QQmlListProperty<AddressItem> AddressBookViewModel::getExpiredAddresses()
{
    return QQmlListProperty<AddressItem>(this, m_expiredAddresses);
}

QString AddressBookViewModel::nameRole() const
{
    return "name";
}

QString AddressBookViewModel::addressRole() const
{
    return "address";
}

QString AddressBookViewModel::categoryRole() const
{
    return "category";
}

QString AddressBookViewModel::expirationRole() const
{
    return "expirationDate";
}

QString AddressBookViewModel::createdRole() const
{
    return "createDate";
}

void AddressBookViewModel::deleteAddress(const QString& addr)
{
    WalletID walletID;
    walletID.FromHex(addr.toStdString());

    m_model.getAsync()->deleteAddress(walletID);
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
        m_activeAddresses.clear();
        m_expiredAddresses.clear();

        for (const auto& addr : addresses)
        {
            if (addr.isExpired())
            {
                m_expiredAddresses.push_back(new AddressItem(addr));
            }
            else
            {
                m_activeAddresses.push_back(new AddressItem(addr));
            }
        }

        emit activeAddressesChanged();
        emit expiredAddressesChanged();
    }
    else
    {
        m_contacts.clear();

        for (const auto& addr : addresses)
        {
            m_contacts.push_back(new ContactItem(addr));
        }

        emit contactsChanged();
    }
}

void AddressBookViewModel::getAddressesFromModel()
{
    m_model.getAsync()->getAddresses(true);
    m_model.getAsync()->getAddresses(false);
}
