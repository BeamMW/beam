#pragma once

#include <QObject>
#include <QThread>

#include "wallet/wallet_db.h"

class WalletModel : public QObject
{
	Q_OBJECT
public:
	WalletModel(beam::IKeyChain::Ptr keychain);
	WalletModel::~WalletModel();

	void start();
signals:
	void onStatus(const beam::Amount& amount);

private slots:
	void statusReq();

private:
	QThread _thread;

	beam::IKeyChain::Ptr _keychain;
};
