#pragma once

#include <QObject>
#include <QThread>

#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"

class WalletModel : public QThread
{
	Q_OBJECT
public:
	WalletModel(beam::IKeyChain::Ptr keychain);
	WalletModel::~WalletModel();

	void run() override;

signals:
	void onStatus(const beam::Amount& amount);

private:

	beam::IKeyChain::Ptr _keychain;
	std::shared_ptr<beam::WalletNetworkIO> _wallet_io;
};
