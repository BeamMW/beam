#include "wallet.h"
#include "utility/logger.h"

namespace
{
	using namespace beam;

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

WalletModel::WalletModel(beam::IKeyChain::Ptr keychain)
	: _keychain(keychain)
{
	qRegisterMetaType<beam::Amount>("beam::Amount");
}

WalletModel::~WalletModel()
{
	auto wallet_io = _wallet_io.lock();

	if (wallet_io)
	{
		wallet_io->stop();
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

		beam::io::Reactor::Ptr reactor(io::Reactor::create());

		// TODO: move port/addr to the config?
		int port = 10000;
		io::Address node_addr;

		if(node_addr.resolve("127.0.0.1:9999"))
		{
			auto wallet_io = std::make_shared<WalletNetworkIO>( io::Address().ip(INADDR_ANY).port(port)
				, node_addr
				, true
				, _keychain
				, reactor);

			_wallet_io = wallet_io;

			struct KeychainSubscriber
			{
				KeychainSubscriber(IKeyChainObserver* client, beam::IKeyChain::Ptr keychain)
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

			wallet_io->start();
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
