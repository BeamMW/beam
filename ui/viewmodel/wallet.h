#pragma once

#include <QObject>
#include <QtCore/qvariant.h>

#include "model/wallet.h"

class TxObject : public QObject
{
	Q_OBJECT

		Q_PROPERTY(QString date			READ date		NOTIFY dateChanged)
		Q_PROPERTY(QString user			READ user		NOTIFY userChanged)
		Q_PROPERTY(QString comment		READ comment	NOTIFY commentChanged)
		Q_PROPERTY(QString amount		READ amount		NOTIFY amountChanged)
		Q_PROPERTY(QString amountUsd	READ amountUsd	NOTIFY amountUsdChanged)
		Q_PROPERTY(QString status		READ status		NOTIFY statusChanged)

public:
	TxObject(const QString& dateVal
		, const QString& userVal
		, const QString& commentVal
		, const QString& amountVal
		, const QString& amountUsdVal
		, const QString& statusVal);

	QString date() const;
	QString user() const;
	QString comment() const;
	QString amount() const;
	QString amountUsd() const;
	QString status() const;

signals:
	void dateChanged();
	void userChanged();
	void commentChanged();
	void amountChanged();
	void amountUsdChanged();
	void statusChanged();

public:
	QString _date;
	QString _user;
	QString _comment;
	QString _amount;
	QString _amountUsd;
	QString _status;

};

class WalletViewModel : public QObject
{
	Q_OBJECT

	Q_PROPERTY(QString available READ available NOTIFY availableChanged)
	Q_PROPERTY(QVariant tx READ tx)

public:
	using TxList = QList<QObject*>;

	WalletViewModel(beam::IKeyChain::Ptr keychain);
	QString available() const;
	QVariant tx() const;

public slots:
	void onStatus(const beam::Amount& amount);

signals:
	void availableChanged();

private:

	beam::Amount _available;
	TxList _tx;

	WalletModel _model;
};
