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
#include "model/qr.h"

using namespace std;
using namespace beam;
using namespace beam::wallet;
using namespace beamui;

namespace
{
    template<typename T>
    bool compare(const T& lf, const T& rt, Qt::SortOrder sortOrder)
    {
        if (sortOrder == Qt::DescendingOrder)
            return lf > rt;
        return lf < rt;
    }
}

AddressItem::AddressItem(const beam::wallet::WalletAddress& address)
    : m_walletAddress(address)
{

}

QString AddressItem::getAddress() const
{
    return beamui::toString(m_walletAddress.m_walletID);
}

QString AddressItem::getName() const
{
    return QString::fromStdString(m_walletAddress.m_label);
}

QString AddressItem::getCategory() const
{
    return QString::fromStdString(m_walletAddress.m_category);
}

QDateTime AddressItem::getExpirationDate() const
{
    QDateTime datetime;
    datetime.setTime_t(m_walletAddress.getExpirationTime());
    
    return datetime;
}

QDateTime AddressItem::getCreateDate() const
{
    QDateTime datetime;
    datetime.setTime_t(m_walletAddress.getCreateTime());
    
    return datetime;
}

bool AddressItem::isNeverExpired() const
{
    return (m_walletAddress.m_duration == 0);
}

bool AddressItem::isExpired() const
{
    return m_walletAddress.isExpired();
}

beam::Timestamp AddressItem::getCreateTimestamp() const
{
    return m_walletAddress.getCreateTime();
}

beam::Timestamp AddressItem::getExpirationTimestamp() const
{
    return m_walletAddress.getExpirationTime();
}

ContactItem::ContactItem(const beam::wallet::WalletAddress& address)
    : m_walletAddress(address)
{

}

QString ContactItem::getAddress() const
{
    return beamui::toString(m_walletAddress.m_walletID);
}

QString ContactItem::getName() const
{
    return QString::fromStdString(m_walletAddress.m_label);
}

QString ContactItem::getCategory() const
{
    return QString::fromStdString(m_walletAddress.m_category);
}

AddressBookViewModel::AddressBookViewModel()
    : m_model{*AppModel::getInstance().getWallet()}
{
    connect(&m_model,
            SIGNAL(addressesChanged(bool, const std::vector<beam::wallet::WalletAddress>&)),
            SLOT(onAddresses(bool, const std::vector<beam::wallet::WalletAddress>&)));
    connect(&m_model,
            SIGNAL(transactionsChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&)),
            SLOT(onTransactions(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&)));

    getAddressesFromModel();
    m_model.getAsync()->getTransactions();
    startTimer(3 * 1000);
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

Qt::SortOrder AddressBookViewModel::activeAddrSortOrder() const
{
    return m_activeAddrSortOrder;
}

Qt::SortOrder AddressBookViewModel::expiredAddrSortOrder() const
{
    return m_expiredAddrSortOrder;
}

Qt::SortOrder AddressBookViewModel::contactSortOrder() const
{
    return m_contactSortOrder;
}

void AddressBookViewModel::setActiveAddrSortOrder(Qt::SortOrder value)
{
    m_activeAddrSortOrder = value;
    sortActiveAddresses();
}

void AddressBookViewModel::setExpiredAddrSortOrder(Qt::SortOrder value)
{
    m_expiredAddrSortOrder = value;
    sortExpiredAddresses();
}

void AddressBookViewModel::setContactSortOrder(Qt::SortOrder value)
{
    m_contactSortOrder = value;
    sortContacts();
}

QString AddressBookViewModel::activeAddrSortRole() const
{
    return m_activeAddrSortRole;
}

QString AddressBookViewModel::expiredAddrSortRole() const
{
    return m_expiredAddrSortRole;
}

QString AddressBookViewModel::contactSortRole() const
{
    return m_contactSortRole;
}

void AddressBookViewModel::setActiveAddrSortRole(QString value)
{
    m_activeAddrSortRole = value;
    sortActiveAddresses();
}

void AddressBookViewModel::setExpiredAddrSortRole(QString value)
{
    m_expiredAddrSortRole = value;
    sortExpiredAddresses();
}

void AddressBookViewModel::setContactSortRole(QString value)
{
    m_contactSortRole = value;
    sortContacts();
}

bool AddressBookViewModel::isAddressBusy(const QString& addr)
{
    WalletID walletID;
    walletID.FromHex(addr.toStdString());
    return find(m_busyAddresses.cbegin(), m_busyAddresses.cend(), walletID) != m_busyAddresses.cend();
}

void AddressBookViewModel::deleteAddress(const QString& addr)
{
    WalletID walletID;
    walletID.FromHex(addr.toStdString());

    m_model.getAsync()->deleteAddress(walletID);
}

void AddressBookViewModel::saveChanges(const QString& addr, const QString& name, uint expirationStatus)
{
    WalletID walletID;
    walletID.FromHex(addr.toStdString());

    m_model.getAsync()->updateAddress(walletID, name.toStdString(), static_cast<WalletAddress::ExpirationStatus>(expirationStatus));
}

// static
QString AddressBookViewModel::generateQR(
        const QString& addr, uint width, uint height)
{
    QR qr(addr, width, height);
    return qr.getEncoded();
}

// static
bool AddressBookViewModel::isAddressWithCommentExist(const QString& comment) const
{
    return m_model.isAddressWithCommentExist(comment.toStdString());
}

void AddressBookViewModel::onAddresses(bool own, const std::vector<beam::wallet::WalletAddress>& addresses)
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

        sortActiveAddresses();
        sortExpiredAddresses();
    }
    else
    {
        m_contacts.clear();

        for (const auto& addr : addresses)
        {
            m_contacts.push_back(new ContactItem(addr));
        }

        sortContacts();
    }
}

void AddressBookViewModel::onTransactions(beam::wallet::ChangeAction action, const std::vector<beam::wallet::TxDescription>& transactions)
{
    switch (action)
    {
        case ChangeAction::Reset:
            {
                m_busyAddresses.clear();
                // no beak!
            }

        case ChangeAction::Added:
            {
                for (const auto& tx : transactions)
                {
                    if (!tx.canDelete())    // only active transactions
                    {
                        m_busyAddresses.push_back(tx.m_myId);
                    }
                }
                break;
            }
        
        case ChangeAction::Updated:
            {
                for (const auto& tx : transactions)
                {
                    auto it = find(m_busyAddresses.cbegin(), m_busyAddresses.cend(), tx.m_myId);
                    if (it != m_busyAddresses.cend() && tx.canDelete())
                    {
                        m_busyAddresses.erase(it);
                    }
                }
                break;
            }

        case ChangeAction::Removed:
            {
                for (const auto& tx : transactions)
                {
                    auto it = find(m_busyAddresses.cbegin(), m_busyAddresses.cend(), tx.m_myId);
                    if (it != m_busyAddresses.cend())
                    {
                        m_busyAddresses.erase(it);
                    }
                }
                break;
            }

        default:
            assert(false && "Unexpected action");
            break;
    }
}

void AddressBookViewModel::timerEvent(QTimerEvent *event)
{
    auto firstExpired = std::remove_if(
        m_activeAddresses.begin(), m_activeAddresses.end(), 
        [](const AddressItem* addr) { return addr->isExpired(); });

    if (firstExpired != m_activeAddresses.end())
    {
        for (auto it = firstExpired; it != m_activeAddresses.end(); ++it)
        {
            m_expiredAddresses.push_back(*it);
        }

        m_activeAddresses.erase(firstExpired, m_activeAddresses.end());

        emit activeAddressesChanged();
        sortExpiredAddresses();
    }
}

void AddressBookViewModel::getAddressesFromModel()
{
    m_model.getAsync()->getAddresses(true);
    m_model.getAsync()->getAddresses(false);
}

void AddressBookViewModel::sortActiveAddresses()
{
    auto cmp = generateAddrComparer(m_activeAddrSortRole, m_activeAddrSortOrder);
    std::sort(m_activeAddresses.begin(), m_activeAddresses.end(), cmp);

    emit activeAddressesChanged();
}

void AddressBookViewModel::sortExpiredAddresses()
{
    auto cmp = generateAddrComparer(m_expiredAddrSortRole, m_expiredAddrSortOrder);
    std::sort(m_expiredAddresses.begin(), m_expiredAddresses.end(), cmp);

    emit expiredAddressesChanged();
}

void AddressBookViewModel::sortContacts()
{
    auto cmp = generateContactComparer();
    std::sort(m_contacts.begin(), m_contacts.end(), cmp);

    emit contactsChanged();
}

std::function<bool(const AddressItem*, const AddressItem*)> AddressBookViewModel::generateAddrComparer(QString role, Qt::SortOrder order)
{
    if (role == nameRole())
        return [sortOrder = order](const AddressItem* lf, const AddressItem* rt)
    {
        return compare(lf->getName(), rt->getName(), sortOrder);
    };

    if (role == addressRole())
        return [sortOrder = order](const AddressItem* lf, const AddressItem* rt)
    {
        return compare(lf->getAddress(), rt->getAddress(), sortOrder);
    };

    if (role == categoryRole())
        return [sortOrder = order](const AddressItem* lf, const AddressItem* rt)
    {
        return compare(lf->getCategory(), rt->getCategory(), sortOrder);
    };

    if (role == expirationRole())
        return [sortOrder = order](const AddressItem* lf, const AddressItem* rt)
    {
        return compare(lf->getExpirationTimestamp(), rt->getExpirationTimestamp(), sortOrder);
    };

    // default for createdRole
    return [sortOrder = order](const AddressItem* lf, const AddressItem* rt)
    {
        return compare(lf->getCreateTimestamp(), rt->getCreateTimestamp(), sortOrder);
    };
}

std::function<bool(const ContactItem*, const ContactItem*)> AddressBookViewModel::generateContactComparer()
{
    if (m_contactSortRole == addressRole())
        return [sortOrder = m_contactSortOrder](const ContactItem* lf, const ContactItem* rt)
    {
        return compare(lf->getAddress(), rt->getAddress(), sortOrder);
    };

    if (m_contactSortRole == categoryRole())
        return [sortOrder = m_contactSortOrder](const ContactItem* lf, const ContactItem* rt)
    {
        return compare(lf->getCategory(), rt->getCategory(), sortOrder);
    };

    // default for nameRole
    return [sortOrder = m_contactSortOrder](const ContactItem* lf, const ContactItem* rt)
    {
        return compare(lf->getName(), rt->getName(), sortOrder);
    };
}
