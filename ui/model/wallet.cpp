#include "wallet.h"
#include "utility/logger.h"
#include "utility/bridge.h"
#include "utility/io/asyncevent.h"

using namespace beam;
using namespace beam::io;
using namespace std;

struct WalletModelBridge : public Bridge<IWalletModelAsync>
{
    BRIDGE_INIT(WalletModelBridge);
    void sendMoney(beam::WalletID receiverID, Amount&& amount, Amount&& fee) override
    {
        tx.send([receiverID, amount{ move(amount) }, fee{ move(fee) }](BridgeInterface& receiver) mutable
        { 
            receiver.sendMoney(receiverID, move(amount), move(fee));
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
};

WalletModel::WalletModel(IKeyChain::Ptr keychain, uint16_t port, const string& nodeAddr)
	: _keychain(keychain)
	, _port(port)
	, _nodeAddrString(nodeAddr)
{
	qRegisterMetaType<WalletStatus>("WalletStatus");
	qRegisterMetaType<vector<TxDescription>>("std::vector<beam::TxDescription>");
	qRegisterMetaType<vector<TxPeer>>("std::vector<beam::TxPeer>");
    qRegisterMetaType<Amount>("beam::Amount");
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
		emit onStatus(getStatus());
		emit onTxStatus(_keychain->getTxHistory());
		emit onTxPeerUpdated(_keychain->getPeers());

		_reactor = Reactor::create();

		Address node_addr;

		if(node_addr.resolve(_nodeAddrString.c_str()))
		{
			auto wallet_io = make_shared<WalletNetworkIO>( Address().ip(INADDR_ANY).port(_port)
				, node_addr
				, true
				, _keychain
				, _reactor);
            _wallet_io = wallet_io;
            auto wallet = make_shared<Wallet>(_keychain, wallet_io);
            _wallet = wallet;

			async = make_shared<WalletModelBridge>(*(static_cast<IWalletModelAsync*>(this)), _reactor);

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
				IWalletObserver* _client;
				std::shared_ptr<beam::Wallet> _wallet;
			};

			WalletSubscriber subscriber(this, wallet);

			wallet_io->start();
		}
		else
		{
			LOG_ERROR() << "unable to resolve node address";
		}
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

void WalletModel::onSyncProgress(int done, int total)
{
	emit onSyncProgressUpdated(done, total);
}

void WalletModel::sendMoney(beam::WalletID receiver, Amount&& amount, Amount&& fee)
{
    assert(!_wallet.expired());
    auto s = _wallet.lock();
    if (s)
    {
        s->transfer_money(receiver, move(amount), move(fee));
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