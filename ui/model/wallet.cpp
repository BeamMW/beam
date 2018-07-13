#include "wallet.h"
#include "utility/logger.h"
#include "utility/io/asyncevent.h"

using namespace beam;
using namespace beam::io;

struct WalletModelAsync : IWalletModelAsync
{
	WalletModelAsync(Reactor::Ptr reactor, std::shared_ptr<WalletNetworkIO> wallet_io)
		: _reactor(reactor) 
		, _wallet_io(wallet_io)
	{}

	void sendMoney(Address&& receiver, Amount&& amount) override
	{
		_sendMoneyEvent = AsyncEvent::create(_reactor, [this, receiver = std::move(receiver), amount = std::move(amount)]() mutable
			{
//				_wallet_io->transfer_money(receiver, std::move(amount), {});
			}
		);

		_sendMoneyEvent->post();
	}
private:
	Reactor::Ptr _reactor;
	std::shared_ptr<WalletNetworkIO> _wallet_io;

	AsyncEvent::Ptr _sendMoneyEvent;
};

WalletModel::WalletModel(IKeyChain::Ptr keychain, uint16_t port, const std::string& nodeAddr)
	: _keychain(keychain)
	, _port(port)
	, _nodeAddrString(nodeAddr)
{
	qRegisterMetaType<Amount>("beam::Amount");
	qRegisterMetaType<std::vector<TxDescription>>("std::vector<beam::TxDescription>");
}

WalletModel::~WalletModel()
{
	if (_wallet_io)
	{
		_wallet_io->stop();
		wait();
	}
}

void WalletModel::run()
{
	try
	{
		emit onStatus(wallet::getAvailable(_keychain));
		emit onTxStatus(_keychain->getTxHistory());

		_reactor = Reactor::create();

		Address node_addr;

		if(node_addr.resolve(_nodeAddrString.c_str()))
		{
			/*_wallet_io = std::make_shared<WalletNetworkIO>( Address().ip(INADDR_ANY).port(_port)
				, node_addr
				, true
				, _keychain
				, _reactor);*/

			async = std::make_shared<WalletModelAsync>(_reactor, _wallet_io);

			struct KeychainSubscriber
			{
				KeychainSubscriber(IKeyChainObserver* client, IKeyChain::Ptr keychain)
					: _client(client)
					, _keychain(keychain)
				{
					_keychain->subscribe(_client);
				}

				~KeychainSubscriber()
				{
					_keychain->unsubscribe(_client);
				}
			private:
				IKeyChainObserver* _client;
				IKeyChain::Ptr _keychain;
			};

			KeychainSubscriber subscriber(this, _keychain);

			_wallet_io->start();
		}
		else
		{
			LOG_ERROR() << "unable to resolve node address";
		}
	}
	catch (const std::runtime_error& e)
	{
		LOG_ERROR() << e.what();
	}
	catch (...)
	{
		LOG_ERROR() << "Unhandled exception";
	}
}

void WalletModel::onKeychainChanged()
{
	emit onStatus(wallet::getAvailable(_keychain));
}

void WalletModel::onTransactionChanged()
{
	emit onTxStatus(_keychain->getTxHistory());
}

void WalletModel::onSystemStateChanged()
{
	emit onStatus(wallet::getAvailable(_keychain));
}
