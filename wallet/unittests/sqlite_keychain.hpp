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

    uint64_t getNextID() override
	{
		return _keychain->getNextID();
	}

	ECC::Scalar calcKey(uint64_t id) override
	{
		return _keychain->calcKey(id);
	}

	std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock = true) override
	{
		return _keychain->getCoins(amount, lock);
	}

	void store(const beam::Coin& coin)
	{
		return _keychain->store(coin);
	}

	void update(const std::vector<beam::Coin>& coins) override
	{
		_keychain->update(coins);
	}

	void remove(const std::vector<beam::Coin>& coins) override
	{
		_keychain->remove(coins);
	}

	void visit(std::function<bool(const beam::Coin& coin)> func) override
	{
		_keychain->visit(func);
	}

	void setLastStateHash(const ECC::Hash::Value& hash) override
	{
		_keychain->setLastStateHash(hash);
	}

	void getLastStateHash(ECC::Hash::Value& hash) const override
	{
		_keychain->getLastStateHash(hash);
	}

private:
	beam::IKeyChain::Ptr _keychain;
};
