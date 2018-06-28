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
	_thread.quit();
	_thread.wait();
}

void WalletModel::start()
{
	moveToThread(&_thread);
	connect(&_thread, SIGNAL(started()), SLOT(statusReq()));
	_thread.start();
}

void WalletModel::statusReq()
{
	emit onStatus(getAvailable(_keychain));
}
