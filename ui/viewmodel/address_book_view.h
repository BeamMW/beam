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


#pragma once

#include <QObject>
#include <QtCore/qvariant.h>
#include <QDateTime>
#include <QQmlListProperty>
#include "wallet/wallet_db.h"
#include "model/wallet_model.h"

class AddressItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address          READ getAddress         CONSTANT)
    Q_PROPERTY(QString name             READ getName            CONSTANT)
    Q_PROPERTY(QString category         READ getCategory        CONSTANT)
    Q_PROPERTY(QDateTime expirationDate READ getExpirationDate  CONSTANT)
    Q_PROPERTY(QDateTime createDate     READ getCreateDate      CONSTANT)
    Q_PROPERTY(bool neverExpired        READ isNeverExpired     CONSTANT)

public:

    AddressItem() = default;
    AddressItem(const beam::wallet::WalletAddress&);

    QString getAddress() const;
    QString getName() const;
    QString getCategory() const;
    QDateTime getExpirationDate() const;
    QDateTime getCreateDate() const;
    bool isNeverExpired() const;

    bool isExpired() const;
    beam::Timestamp getCreateTimestamp() const;
    beam::Timestamp getExpirationTimestamp() const;

private:
    beam::wallet::WalletAddress m_walletAddress;
};

class ContactItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address       READ getAddress    CONSTANT)
    Q_PROPERTY(QString name          READ getName       CONSTANT)
    Q_PROPERTY(QString category      READ getCategory   CONSTANT)

public:
    ContactItem() = default;
    ContactItem(const beam::wallet::WalletAddress&);

    QString getAddress() const;
    QString getName() const;
    QString getCategory() const;

private:
    beam::wallet::WalletAddress m_walletAddress;
};

class AddressBookViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQmlListProperty<ContactItem> contacts   READ getContacts   NOTIFY contactsChanged)
    Q_PROPERTY(QQmlListProperty<AddressItem> activeAddresses   READ getActiveAddresses   NOTIFY activeAddressesChanged)
    Q_PROPERTY(QQmlListProperty<AddressItem> expiredAddresses   READ getExpiredAddresses   NOTIFY expiredAddressesChanged)

    Q_PROPERTY(QString nameRole READ nameRole CONSTANT)
    Q_PROPERTY(QString addressRole READ addressRole CONSTANT)
    Q_PROPERTY(QString categoryRole READ categoryRole CONSTANT)
    Q_PROPERTY(QString expirationRole READ expirationRole CONSTANT)
    Q_PROPERTY(QString createdRole READ createdRole CONSTANT)

    Q_PROPERTY(Qt::SortOrder activeAddrSortOrder READ activeAddrSortOrder WRITE setActiveAddrSortOrder)
    Q_PROPERTY(Qt::SortOrder expiredAddrSortOrder READ expiredAddrSortOrder WRITE setExpiredAddrSortOrder)
    Q_PROPERTY(Qt::SortOrder contactSortOrder READ contactSortOrder WRITE setContactSortOrder)

    Q_PROPERTY(QString activeAddrSortRole READ activeAddrSortRole WRITE setActiveAddrSortRole)
    Q_PROPERTY(QString expiredAddrSortRole READ expiredAddrSortRole WRITE setExpiredAddrSortRole)
    Q_PROPERTY(QString contactSortRole READ contactSortRole WRITE setContactSortRole)

public:
    Q_INVOKABLE bool isAddressBusy(const QString& addr);
    Q_INVOKABLE void deleteAddress(const QString& addr);
    Q_INVOKABLE void saveChanges(const QString& addr, const QString& name, uint expirationStatus);
    Q_INVOKABLE static QString generateQR(const QString& addr, uint width, uint height);
    Q_INVOKABLE bool isAddressWithCommentExist(const QString& comment) const;

public:

    AddressBookViewModel();

    QQmlListProperty<ContactItem> getContacts();
    QQmlListProperty<AddressItem> getActiveAddresses();
    QQmlListProperty<AddressItem> getExpiredAddresses();

    QString nameRole() const;
    QString addressRole() const;
    QString categoryRole() const;
    QString expirationRole() const;
    QString createdRole() const;

    Qt::SortOrder activeAddrSortOrder() const;
    Qt::SortOrder expiredAddrSortOrder() const;
    Qt::SortOrder contactSortOrder() const;
    void setActiveAddrSortOrder(Qt::SortOrder);
    void setExpiredAddrSortOrder(Qt::SortOrder);
    void setContactSortOrder(Qt::SortOrder);

    QString activeAddrSortRole() const;
    QString expiredAddrSortRole() const;
    QString contactSortRole() const;
    void setActiveAddrSortRole(QString);
    void setExpiredAddrSortRole(QString);
    void setContactSortRole(QString);

public slots:
    void onAddresses(bool own, const std::vector<beam::wallet::WalletAddress>& addresses);
    void onTransactions(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&);

signals:
    void contactsChanged();
    void activeAddressesChanged();
    void expiredAddressesChanged();

protected:

    void timerEvent(QTimerEvent *event) override;

private:

    void getAddressesFromModel();
    void sortActiveAddresses();
    void sortExpiredAddresses();
    void sortContacts();

    std::function<bool(const AddressItem*, const AddressItem*)> generateAddrComparer(QString, Qt::SortOrder);
    std::function<bool(const ContactItem*, const ContactItem*)> generateContactComparer();

private:
    WalletModel& m_model;
    QList<ContactItem*> m_contacts;
    QList<AddressItem*> m_activeAddresses;
    QList<AddressItem*> m_expiredAddresses;
    Qt::SortOrder m_activeAddrSortOrder;
    Qt::SortOrder m_expiredAddrSortOrder;
    Qt::SortOrder m_contactSortOrder;
    QString m_activeAddrSortRole;
    QString m_expiredAddrSortRole;
    QString m_contactSortRole;
    std::vector<beam::wallet::WalletID> m_busyAddresses;
};
