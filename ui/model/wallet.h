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
#include <QThread>

#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"

struct IWalletModelAsync
{
	using Ptr = std::shared_ptr<IWalletModelAsync>;

    virtual void sendMoney(beam::WalletID sender, beam::WalletID receiver, beam::Amount&& amount, beam::Amount&& fee = 0) = 0;
	virtual void syncWithNode() = 0;
    virtual void calcChange(beam::Amount&& amount) = 0;
    virtual void getAvaliableUtxos() = 0;
	virtual void getAllUtxos() = 0;
    virtual void getAddresses(bool own) = 0;
    virtual void cancelTx(beam::TxID id) = 0;
    virtual void createNewAddress(beam::WalletAddress&& address) = 0;
	virtual void generateNewWalletID() = 0;
	virtual void changeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID) = 0;

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
    , private IWalletModelAsync
{
	Q_OBJECT
public:
	WalletModel(beam::IKeyChain::Ptr keychain, beam::IKeyStore::Ptr keystore, const std::string& nodeAddr);
	~WalletModel();

	void run() override;

public:
	IWalletModelAsync::Ptr async;

signals:
	void onStatus(const WalletStatus& status);
	void onTxStatus(const std::vector<beam::TxDescription>& history);
	void onTxPeerUpdated(const std::vector<beam::TxPeer>& peers);
	void onSyncProgressUpdated(int done, int total);
    void onChangeCalculated(beam::Amount change);
    void onUtxoChanged(const std::vector<beam::Coin>& utxos);
	void onAllUtxoChanged(const std::vector<beam::Coin>& utxos);
	void onAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses);
	void onGeneratedNewWalletID(const beam::WalletID& walletID);
	void onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID);

private:
	void onKeychainChanged() override;
	void onTransactionChanged() override;
	void onSystemStateChanged() override;
	void onTxPeerChanged() override;
    void onAddressChanged() override;
	void onSyncProgress(int done, int total) override;

    void sendMoney(beam::WalletID sender, beam::WalletID receiver, beam::Amount&& amount, beam::Amount&& fee) override;
    void syncWithNode() override;
    void calcChange(beam::Amount&& amount) override;
    void getAvaliableUtxos() override;
	void getAllUtxos() override;
    void getAddresses(bool own) override;
    void cancelTx(beam::TxID id) override;
    void createNewAddress(beam::WalletAddress&& address) override;
	void changeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID) override;
	void generateNewWalletID() override;

	void onStatusChanged();
	WalletStatus getStatus() const;
    std::vector<beam::Coin> getUtxos(bool all = false) const;
private:

	uint16_t _port;
	std::string _nodeAddrString;

	beam::IKeyChain::Ptr _keychain;
    beam::IKeyStore::Ptr _keystore;
    beam::io::Reactor::Ptr _reactor;
    std::weak_ptr<beam::INetworkIO> _wallet_io;
    std::weak_ptr<beam::Wallet> _wallet;
    beam::io::Timer::Ptr _logRotateTimer;
};
