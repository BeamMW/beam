#pragma once

#include "wallet/keychain.h"

#include <boost/filesystem.hpp>

static const char* Pass = "pass123";

struct SqliteKeychain : beam::IKeyChain
{
	SqliteKeychain()
	{
		const char* dbName = beam::Keychain::getName();

		if(boost::filesystem::exists(dbName))
			boost::filesystem::remove(dbName);

		// init wallet with password
		{
			auto keychain = beam::Keychain::init(Pass);
			assert(keychain != nullptr);
		}

		// open wallet with password
		_keychain = beam::Keychain::open(Pass);
		assert(_keychain != nullptr);
	}

    virtual uint64_t getNextID()
	{
		return _keychain->getNextID();
	}

	virtual ECC::Scalar calcKey(uint64_t id)
	{
		return _keychain->calcKey(id);
	}

	virtual std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock = true)
	{
		return _keychain->getCoins(amount, lock);
	}

	virtual void store(const beam::Coin& coin)
	{
		return _keychain->store(coin);
	}

	virtual void update(const std::vector<beam::Coin>& coins)
	{
		_keychain->update(coins);
	}

	virtual void remove(const std::vector<beam::Coin>& coins)
	{
		_keychain->remove(coins);
	}

private:
	beam::IKeyChain::Ptr _keychain;
};
