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

	ECC::Scalar::Native calcKey(const beam::Coin& coin) const override
	{
		return _keychain->calcKey(coin);
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

	void setVarRaw(const char* name, const void* data, int size) override
	{
		_keychain->setVarRaw(name, data, size);
	}

	int getVarRaw(const char* name, void* data) const override
	{
		return _keychain->getVarRaw(name, data);
	}

private:
	beam::IKeyChain::Ptr _keychain;
};
