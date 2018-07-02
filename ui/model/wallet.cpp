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
	if(_wallet_io)
		_wallet_io->stop();
}

void WalletModel::run()
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
		_wallet_io = std::make_shared<WalletNetworkIO>( io::Address().ip(INADDR_ANY).port(port)
			, node_addr
			, true
			, _keychain
			, reactor);

		_keychain->subscribe(this);

		_wallet_io->start();
	}
	else
	{
		LOG_ERROR() << "unable to resolve node address";
	}
}

void WalletModel::onKeychainChanged()
{
	emit onStatus(getAvailable(_keychain));
}
