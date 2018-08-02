#pragma once

#include <QObject>
#include <QtCore/qvariant.h>
#include "wallet/wallet_db.h"
#include "model/wallet.h"

class PeerAddressItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString walletID      READ getWalletID                     NOTIFY onWalletIDChanged)
    Q_PROPERTY(QString name          READ getName       WRITE setName     NOTIFY onNameChanged)
    Q_PROPERTY(QString category      READ getCategory   WRITE setCategory NOTIFY onCategoryChanged)
public:
    PeerAddressItem(const beam::WalletAddress&);
    virtual ~PeerAddressItem()
    {

    };

    QString getWalletID() const;
    QString getName() const;
    void setName(const QString& value);
    QString getCategory() const;
    void setCategory(const QString& value);
signals:
    void onWalletIDChanged();
    void onNameChanged();
    void onCategoryChanged();
private:
    QString m_walletID;
    QString m_name;
    QString m_category;
};

class OwnAddressItem : public PeerAddressItem
{
    Q_OBJECT
    Q_PROPERTY(QString expirationDate      READ getExpirationDate         NOTIFY onDateChanged)
    Q_PROPERTY(QString createDate          READ getCreateDate             NOTIFY onDateChanged)
public:
    OwnAddressItem(const beam::WalletAddress&);

    QString getExpirationDate() const;
    QString getCreateDate() const;

signals:
    void onDateChanged();
private:
    QString m_expirationDate;
    QString m_createDate;
};


class AddressBookViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariant peerAddresses           READ getPeerAddresses   NOTIFY addressesChanged)
    Q_PROPERTY(QVariant ownAddresses            READ getOwnAddresses    NOTIFY addressesChanged)

public:

    AddressBookViewModel(WalletModel& model);

    QVariant getPeerAddresses() const;
    QVariant getOwnAddresses() const;

public slots:
    void OnAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses);

signals:
    void addressesChanged();

private:
    WalletModel& m_model;
    QList<QObject*> m_peerAddresses;
    QList<QObject*> m_ownAddresses;
};