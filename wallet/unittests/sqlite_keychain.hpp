#pragma once

#include "wallet/wallet_db.h"

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
            ECC::NoLeak<ECC::uintBig> seed;
            seed.V = ECC::Zero;
			auto keychain = beam::Keychain::init(Pass, seed);
			assert(keychain != nullptr);
		}

		// open wallet with password
		_keychain = beam::Keychain::open(Pass);
		assert(_keychain != nullptr);

		beam::Block::SystemState::ID id = { 0 };
		id.m_Height = 134;
		setSystemStateID(id);
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

	void setVarRaw(const char* name, const void* data, int size) override
	{
		_keychain->setVarRaw(name, data, size);
	}

	int getVarRaw(const char* name, void* data) const override
	{
		return _keychain->getVarRaw(name, data);
	}

	void setSystemStateID(const beam::Block::SystemState::ID& stateID)
	{
		_keychain->setSystemStateID(stateID);
	}

	bool getSystemStateID(beam::Block::SystemState::ID& stateID) const
	{
		return _keychain->getSystemStateID(stateID);
	}

    beam::Height getCurrentHeight() const override
    {
        return _keychain->getCurrentHeight();
    }

    std::vector<beam::TxDescription> getTxHistory(uint64_t start, size_t count) override
    {
        return _keychain->getTxHistory(start, count);
    }

    boost::optional<beam::TxDescription> getTx(const beam::Uuid& txId) override
    {
        return _keychain->getTx(txId);
    }

    void saveTx(const beam::TxDescription& t) override
    {
        _keychain->saveTx(t);
    }

    void deleteTx(const beam::Uuid& txId) override
    {
        _keychain->deleteTx(txId);
    }

private:
	beam::IKeyChain::Ptr _keychain;
};
