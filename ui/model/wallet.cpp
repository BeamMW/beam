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

#include "wallet.h"
#include "utility/logger.h"
#include "utility/bridge.h"
#include "utility/io/asyncevent.h"

using namespace beam;
using namespace beam::io;
using namespace std;

namespace
{
    static const unsigned LOG_ROTATION_PERIOD = 3 * 60 * 60 * 1000; // 3 hours

	using Nonce = ECC::Scalar::Native;

	// TODO It's temporary solution
	static void gen_nonce(Nonce& nonce) {
		ECC::Scalar sc;
		uint64_t seed;

		// here we want to read as little as possible from slow sources, TODO: review this
		ECC::GenRandom(&seed, 8);
		ECC::Hash::Processor() << seed >> sc.m_Value;

		nonce.Import(sc);
	}

	void gen_keypair(PeerID& pubKey) {
		Nonce privKey;
		gen_nonce(privKey);
		proto::Sk2Pk(pubKey, privKey);
	}
}

struct WalletModelBridge : public Bridge<IWalletModelAsync>
{
    BRIDGE_INIT(WalletModelBridge);

    void sendMoney(beam::WalletID senderID, beam::WalletID receiverID, Amount&& amount, Amount&& fee) override
    {
        tx.send([senderID, receiverID, amount{ move(amount) }, fee{ move(fee) }](BridgeInterface& receiver) mutable
        { 
            receiver.sendMoney(senderID, receiverID, move(amount), move(fee));
        }); 
    }

    void syncWithNode() override
    {
        tx.send([](BridgeInterface& receiver) mutable
        {
            receiver.syncWithNode();
        });
    }

    void calcChange(beam::Amount&& amount) override
    {
        tx.send([amount{move(amount)}](BridgeInterface& receiver) mutable
        {
            receiver.calcChange(move(amount));
        });
    }

    void getAvaliableUtxos() override
    {
        tx.send([](BridgeInterface& receiver) mutable
        {
            receiver.getAvaliableUtxos();
        });
    }

	void getAllUtxos() override
	{
		tx.send([](BridgeInterface& receiver) mutable
		{
			receiver.getAllUtxos();
		});
	}

	void getAddresses(bool own) override
	{
		tx.send([own](BridgeInterface& receiver) mutable
		{
			receiver.getAddresses(own);
		});
	}

    void cancelTx(beam::TxID id) override
    {
        tx.send([id](BridgeInterface& receiver) mutable
        {
            receiver.cancelTx(id);
        });
    }

    void createNewAddress(WalletAddress&& address) override
    {
        tx.send([address{ move(address) }](BridgeInterface& receiver) mutable
        {
            receiver.createNewAddress(move(address));
        });
    }

	void changeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID) override
	{
		tx.send([senderID, receiverID](BridgeInterface& receiver) mutable
		{
			receiver.changeCurrentWalletIDs(senderID, receiverID);
		});
	}

	void generateNewWalletID() override
	{
		tx.send([](BridgeInterface& receiver) mutable
		{
			receiver.generateNewWalletID();
		});
	}
};

WalletModel::WalletModel(IKeyChain::Ptr keychain, IKeyStore::Ptr keystore, const string& nodeAddr)
	: _keychain(keychain)
    , _keystore(keystore)
	, _nodeAddrString(nodeAddr)
{
	qRegisterMetaType<WalletStatus>("WalletStatus");
	qRegisterMetaType<vector<TxDescription>>("std::vector<beam::TxDescription>");
	qRegisterMetaType<vector<TxPeer>>("std::vector<beam::TxPeer>");
    qRegisterMetaType<Amount>("beam::Amount");
    qRegisterMetaType<vector<Coin>>("std::vector<beam::Coin>");
    qRegisterMetaType<vector<WalletAddress>>("std::vector<beam::WalletAddress>");
	qRegisterMetaType<WalletID>("beam::WalletID");
}

WalletModel::~WalletModel() 
{
    try
    {
        if (_reactor)
        {
            _reactor->stop();
            wait();
        }
    }
    catch (...)
    {

    }
}

WalletStatus WalletModel::getStatus() const
{
	WalletStatus status{ wallet::getAvailable(_keychain), 0, 0, 0};

	auto history = _keychain->getTxHistory();

	for (const auto& item : history)
	{
		switch (item.m_status)
		{
		case TxDescription::Completed:
			(item.m_sender ? status.sent : status.received) += item.m_amount;
			break;
		default: break;
		}
	}

	status.unconfirmed += wallet::getTotal(_keychain, Coin::Unconfirmed);

	status.update.lastTime = _keychain->getLastUpdateTime();

	return status;
}

void WalletModel::run()
{
	try
	{
		struct WalletSubscriber
		{
			WalletSubscriber(IWalletObserver* client, std::shared_ptr<beam::Wallet> wallet)
				: _client(client)
				, _wallet(wallet)
			{
				_wallet->subscribe(_client);
			}

			~WalletSubscriber()
			{
				_wallet->unsubscribe(_client);
			}
		private:
			IWalletObserver * _client;
			std::shared_ptr<beam::Wallet> _wallet;
		};

		std::unique_ptr<WalletSubscriber> subscriber;

		_reactor = Reactor::create();
		async = make_shared<WalletModelBridge>(*(static_cast<IWalletModelAsync*>(this)), _reactor);
		
		emit onStatus(getStatus());
		emit onTxStatus(_keychain->getTxHistory());
		emit onTxPeerUpdated(_keychain->getPeers());

		Address node_addr;

		if(node_addr.resolve(_nodeAddrString.c_str()))
		{
			auto wallet_io = make_shared<WalletNetworkIO>(
				node_addr
				, _keychain
                , _keystore
				, _reactor);
            _wallet_io = wallet_io;
            auto wallet = make_shared<Wallet>(_keychain, wallet_io);
            _wallet = wallet;

			_logRotateTimer = io::Timer::create(_reactor);
			_logRotateTimer->start(
				LOG_ROTATION_PERIOD, true,
				[]() {
					Logger::get()->rotate();
				}
			);

			subscriber = make_unique<WalletSubscriber>(static_cast<IWalletObserver*>(this), wallet);
		}
		else
		{
			LOG_ERROR() << "unable to resolve node address";
		}
		
		_reactor->run();
	}
	catch (const runtime_error& e)
	{
		LOG_ERROR() << e.what();
	}
	catch (...)
	{
		LOG_ERROR() << "Unhandled exception";
	}
}

void WalletModel::onStatusChanged()
{
	emit onStatus(getStatus());
}

void WalletModel::onKeychainChanged()
{
	onStatusChanged();
}

void WalletModel::onTransactionChanged()
{
	emit onTxStatus(_keychain->getTxHistory());
	onStatusChanged();
}

void WalletModel::onSystemStateChanged()
{
	onStatusChanged();
}

void WalletModel::onTxPeerChanged()
{
	emit onTxPeerUpdated(_keychain->getPeers());
}

void WalletModel::onAddressChanged()
{
    emit onAdrresses(true, _keychain->getAddresses(true));
    emit onAdrresses(false, _keychain->getAddresses(false));
}

void WalletModel::onSyncProgress(int done, int total)
{
	emit onSyncProgressUpdated(done, total);
}

void WalletModel::sendMoney(beam::WalletID sender, beam::WalletID receiver, Amount&& amount, Amount&& fee)
{
    assert(!_wallet.expired());
    auto s = _wallet.lock();
    if (s)
    {
        s->transfer_money(sender, receiver, move(amount), move(fee));
    }
}

void WalletModel::syncWithNode()
{
    assert(!_wallet_io.expired());
    auto s = _wallet_io.lock();
    if (s)
    {
        static_pointer_cast<INetworkIO>(s)->connect_node();
    }
}

void WalletModel::calcChange(beam::Amount&& amount)
{
    auto coins = _keychain->selectCoins(amount, false);
    Amount sum = 0;
    for (auto& c : coins)
    {
        sum += c.m_amount;
    }
    if (sum < amount)
    {
        emit onChangeCalculated(0);
    }
    else
    {
        emit onChangeCalculated(sum - amount);
    }    
}

void WalletModel::getAvaliableUtxos()
{
    emit onUtxoChanged(getUtxos());
}

void WalletModel::getAllUtxos()
{
	emit onAllUtxoChanged(getUtxos(true));
}

void WalletModel::getAddresses(bool own)
{
	emit onAdrresses(own, _keychain->getAddresses(own));
}

void WalletModel::cancelTx(beam::TxID id)
{
    auto w = _wallet.lock();
    if (w)
    {
        w->cancel_tx(id);
    }
}

void WalletModel::createNewAddress(WalletAddress&& address)
{
    _keychain->saveAddress(address);
}

void WalletModel::changeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID)
{
	emit onChangeCurrentWalletIDs(senderID, receiverID);
}

void WalletModel::generateNewWalletID()
{
	WalletID walletID;
	gen_keypair(walletID);

	emit onGeneratedNewWalletID(walletID);
}

vector<Coin> WalletModel::getUtxos(bool all) const
{
    vector<Coin> utxos;
    auto currentHeight = _keychain->getCurrentHeight();
    _keychain->visit([&utxos, &currentHeight, all](const Coin& c)->bool
    {
		if (all) {
			utxos.push_back(c);
			return true;
		}

        Height lockHeight = c.m_maturity;

        if (c.m_status == Coin::Unspent
            && lockHeight <= currentHeight)
        {
            utxos.push_back(c);
        }
        return true;
    });
    return utxos;
}