#include "wallet.h"
#include "utility/logger.h"
#include "utility/io/asyncevent.h"

namespace
{
	using namespace beam;
	using namespace beam::io;

	// taken from beam.cpp
	// TODO: move 'getAvailable' to one place
	Amount getAvailable(IKeyChain::Ptr keychain)
	{
		auto currentHeight = keychain->getCurrentHeight();
		Amount total = 0;
		keychain->visit([&total, &currentHeight](const Coin& c)->bool
		{
			Height lockHeight = c.m_height + (c.m_key_type == KeyType::Coinbase
				? Rules::MaturityCoinbase
				: Rules::MaturityStd);

			if (c.m_status == Coin::Unspent
				&& lockHeight <= currentHeight)
			{
				total += c.m_amount;
			}

			return true;
		});
		return total;
	}
}

struct WalletModelAsync : IWalletModelAsync
{
	WalletModelAsync(Reactor::Ptr reactor, std::shared_ptr<WalletNetworkIO> wallet_io)
		: _reactor(reactor) 
		, _wallet_io(wallet_io)
	{}

	void sendMoney(const Address& receiver, Amount&& amount) override
	{
		_sendMoneyEvent = AsyncEvent::create(_reactor, [&]()
			{
			_wallet_io->transfer_money(receiver, std::move(amount), {});
			}
		);

		_sendMoneyEvent->trigger();
	}
private:
	Reactor::Ptr _reactor;
	std::shared_ptr<WalletNetworkIO> _wallet_io;

	AsyncEvent::Ptr _sendMoneyEvent;
};

WalletModel::WalletModel(IKeyChain::Ptr keychain)
	: _keychain(keychain)
{
	qRegisterMetaType<Amount>("beam::Amount");
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
		emit onStatus(getAvailable(_keychain));

		// TODO: read this from the config
		Rules::FakePoW = true;

		_reactor = Reactor::create();

		// TODO: move port/addr to the config?
		int port = 10000;
		Address node_addr;

		if(node_addr.resolve("127.0.0.1:9999"))
		{
			_wallet_io = std::make_shared<WalletNetworkIO>( Address().ip(INADDR_ANY).port(port)
				, node_addr
				, true
				, _keychain
				, _reactor);

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
	emit onStatus(getAvailable(_keychain));
}
