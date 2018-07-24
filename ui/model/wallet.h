#pragma once

#include <QObject>
#include <QThread>

#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"

struct IWalletModelAsync
{
	using Ptr = std::shared_ptr<IWalletModelAsync>;

    virtual void sendMoney(beam::WalletID receiver, beam::Amount&& amount, beam::Amount&& fee = 0) = 0;
	virtual void syncWithNode() = 0;

	virtual ~IWalletModelAsync() {}
};

struct WalletStatus
{
	beam::Amount available;
	beam::Amount received;
	beam::Amount sent;
	beam::Amount unconfirmed;

	struct
	{
		beam::Timestamp lastTime;
		int done;
		int total;
	} update;
};

class WalletModel 
	: public QThread
	, private beam::IWalletObserver
{
	Q_OBJECT
public:
	WalletModel(beam::IKeyChain::Ptr keychain, uint16_t port, const std::string& nodeAddr);
	~WalletModel();

	void run() override;

public:
	IWalletModelAsync::Ptr async;

signals:
	void onStatus(const WalletStatus& status);
	void onTxStatus(const std::vector<beam::TxDescription>& history);
	void onTxPeerUpdated(const std::vector<beam::TxPeer>& peers);
	void onSyncProgressUpdated(int done, int total);

private:
	void onKeychainChanged() override;
	void onTransactionChanged() override;
	void onSystemStateChanged() override;
	void onTxPeerChanged() override;
	void onSyncProgress(int done, int total) override;

	void onStatusChanged();
	WalletStatus getStatus() const;
private:

	uint16_t _port;
	std::string _nodeAddrString;

	beam::IKeyChain::Ptr _keychain;
	beam::io::Reactor::Ptr _reactor;
	std::shared_ptr<beam::WalletNetworkIO> _wallet_io;
    std::shared_ptr<beam::Wallet> _wallet;
};
