#pragma once

#include <QObject>
#include <QtCore/qvariant.h>
#include <QQmlListProperty>
#include "wallet/wallet_db.h"
#include "model/wallet.h"

class PeerAddressItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString walletID      READ getWalletID   WRITE setWalletID NOTIFY onWalletIDChanged)
    Q_PROPERTY(QString name          READ getName       WRITE setName     NOTIFY onNameChanged)
    Q_PROPERTY(QString category      READ getCategory   WRITE setCategory NOTIFY onCategoryChanged)
public:
	PeerAddressItem();
    PeerAddressItem(const beam::WalletAddress&);
    virtual ~PeerAddressItem()
    {

    };

	QString getWalletID() const;
	void setWalletID(const QString& value);
    QString getName() const;
    void setName(const QString& value);
    QString getCategory() const;
    void setCategory(const QString& value);

	virtual void clean();

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
	Q_PROPERTY(QString walletID            READ getWalletID               NOTIFY onWalletIDChanged)
public:
	OwnAddressItem();
    OwnAddressItem(const beam::WalletAddress&);
	void setExpirationDate(const QString&);
	void setCreateDate(const QString&);
    QString getExpirationDate() const;
    QString getCreateDate() const;

	void clean() override;

signals:
    void onDateChanged();
private:
    QString m_expirationDate;
    QString m_createDate;
};

class AddressBookViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QQmlListProperty<PeerAddressItem> peerAddresses   READ getPeerAddresses   NOTIFY addressesChanged)
    Q_PROPERTY(QQmlListProperty<OwnAddressItem>  ownAddresses    READ getOwnAddresses    NOTIFY addressesChanged)

	Q_PROPERTY(PeerAddressItem*                  newPeerAddress  READ getNewPeerAddress  CONSTANT)
	Q_PROPERTY(OwnAddressItem*                   newOwnAddress   READ getNewOwnAddress   CONSTANT)

public:
    Q_INVOKABLE void createNewAddress();
	Q_INVOKABLE void createNewPeerAddress();
	Q_INVOKABLE void createNewOwnAddress();
	Q_INVOKABLE void changeCurrentPeerAddress(int index);
    Q_INVOKABLE void deletePeerAddress(int index);
    Q_INVOKABLE void deleteOwnAddress(int index);

	Q_INVOKABLE void generateNewEmptyAddress();

public:

    AddressBookViewModel(WalletModel& model);

	QQmlListProperty<PeerAddressItem> getPeerAddresses();
	QQmlListProperty<OwnAddressItem> getOwnAddresses();
	PeerAddressItem* getNewPeerAddress();
	OwnAddressItem* getNewOwnAddress();

public slots:
    void onStatus(const WalletStatus& amount);
    void onAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses);
	void onGeneratedNewWalletID(const beam::WalletID& walletID);

signals:
    void addressesChanged();

private:
    WalletModel& m_model;
    QList<PeerAddressItem*> m_peerAddresses;
    QList<OwnAddressItem*> m_ownAddresses;

	OwnAddressItem m_newOwnAddress;
	PeerAddressItem m_newPeerAddress;
};