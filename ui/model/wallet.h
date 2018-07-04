#pragma once

#include <QObject>
#include <QThread>

#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"

struct IWalletModelAsync
{
	using Ptr = std::shared_ptr<IWalletModelAsync>;

	virtual void sendMoney(const beam::io::Address& receiver, beam::Amount&& amount) = 0;

	virtual ~IWalletModelAsync() {}
};

class WalletModel 
	: public QThread
	, private beam::IKeyChainObserver
{
	Q_OBJECT
public:
	WalletModel(beam::IKeyChain::Ptr keychain);
	~WalletModel();

	void run() override;

public:
	IWalletModelAsync::Ptr async;

signals:
	void onStatus(const beam::Amount& amount);
	void onTxStatus(const std::vector<beam::TxDescription>& history);

private:
	void onKeychainChanged() override;
	void onTransactionChanged() override;

private:

	beam::IKeyChain::Ptr _keychain;
	beam::io::Reactor::Ptr _reactor;
	std::shared_ptr<beam::WalletNetworkIO> _wallet_io;
};
