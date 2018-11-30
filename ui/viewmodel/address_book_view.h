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
#include <QQmlListProperty>
#include "wallet/wallet_db.h"
#include "model/wallet_model.h"

class AddressItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address          READ getAddress         CONSTANT)
    Q_PROPERTY(QString name             READ getName            CONSTANT)
    Q_PROPERTY(QString category         READ getCategory        CONSTANT)
    Q_PROPERTY(QString expirationDate   READ getExpirationDate  CONSTANT)
    Q_PROPERTY(QString createDate       READ getCreateDate      CONSTANT)

public:

    AddressItem() = default;
    AddressItem(const beam::WalletAddress&);

    QString getAddress() const;
    QString getName() const;
    QString getCategory() const;
    QString getExpirationDate() const;
    QString getCreateDate() const;

    bool isExpired() const;

private:
    QString m_address;
    QString m_name;
    QString m_category;
    QString m_createDate;
    beam::Timestamp m_expirationDate;
};

class ContactItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address       READ getAddress    CONSTANT)
    Q_PROPERTY(QString name          READ getName       CONSTANT)
    Q_PROPERTY(QString category      READ getCategory   CONSTANT)

public:
    ContactItem() = default;
    ContactItem(const beam::WalletAddress&);

    QString getAddress() const;
    QString getName() const;
    QString getCategory() const;

private:
    QString m_address;
    QString m_name;
    QString m_category;
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

public:

	Q_INVOKABLE void deleteAddress(const QString& addr);
    Q_INVOKABLE void copyToClipboard(const QString& text);

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

public slots:
    void onStatus(const WalletStatus& amount);
    void onAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses);

signals:
    void contactsChanged();
    void activeAddressesChanged();
    void expiredAddressesChanged();

private:

    void getAddressesFromModel();

private:
    WalletModel& m_model;
    QList<ContactItem*> m_contacts;
    QList<AddressItem*> m_activeAddresses;
    QList<AddressItem*> m_expiredAddresses;
};