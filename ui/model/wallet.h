#pragma once

#include <QObject>
#include <QThread>

#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"

class WalletModel 
	: public QThread
	, private beam::IKeyChainObserver
{
	Q_OBJECT
public:
	WalletModel(beam::IKeyChain::Ptr keychain);
	~WalletModel();

	void run() override;

signals:
	void onStatus(const beam::Amount& amount);

private:
	void onKeychainChanged() override;

private:

	beam::IKeyChain::Ptr _keychain;
	std::weak_ptr<beam::WalletNetworkIO> _wallet_io;
};
