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


	ECC::Scalar::Native calcKey(const beam::Coin& coin) const override
	{
		return _keychain->calcKey(coin);
	}

	std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock = true) override
	{
		return _keychain->getCoins(amount, lock);
	}

	void store(beam::Coin& coin)
	{
		return _keychain->store(coin);
	}

    void store(std::vector<beam::Coin>& coins)
    {
        return _keychain->store(coins);
    }

	void update(const std::vector<beam::Coin>& coins) override
	{
		_keychain->update(coins);
	}

	void remove(const std::vector<beam::Coin>& coins) override
	{
		_keychain->remove(coins);
	}

    void remove(const beam::Coin& coin) override
    {
        _keychain->remove(coin);
    }

	void visit(std::function<bool(const beam::Coin& coin)> func) override
	{
		_keychain->visit(func);
	}

    void visitMinedCoins(beam::Height minHeight, std::function<bool(const beam::Coin& coin)> func) override
    {
        _keychain->visitMinedCoins(minHeight, func);
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
