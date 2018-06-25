#include "wallet.h"

using namespace beam;

TxObject::TxObject(const QString& dateVal
	, const QString& userVal
	, const QString& commentVal
	, const QString& amountVal
	, const QString& amountUsdVal
	, const QString& statusVal)
	: _date(dateVal)
	, _user(userVal)
	, _comment(commentVal)
	, _amount(amountVal)
	, _amountUsd(amountUsdVal)
	, _status(statusVal)
{

}

QString TxObject::date() const
{
	return _date;
}

QString TxObject::user() const
{
	return _user;
}

QString TxObject::comment() const
{
	return _comment;
}

QString TxObject::amount() const
{
	return _amount;
}

QString TxObject::amountUsd() const
{
	return _amountUsd;

}

QString TxObject::status() const
{
	return _status;
}

WalletViewModel::WalletViewModel(IKeyChain::Ptr keychain)
	: _keychain(keychain)
{
	_tx.append(new TxObject(
		"12 June 2018 | 3:46 PM"
		, "super_user"
		, "Beam is super cool, bla bla bla..."
		, "+0.63736 BEAM"
		, "726.4 USD"
		, "unspent"));
	_tx.append(new TxObject(
		"12 June 2018 | 3:46 PM"
		, "super_user"
		, "Beam is super cool, bla bla bla..."
		, "+0.63736 BEAM"
		, "726.4 USD"
		, "unspent"));
	_tx.append(new TxObject(
		"12 June 2018 | 3:46 PM"
		, "super_user"
		, "Beam is super cool, bla bla bla..."
		, "+0.63736 BEAM"
		, "726.4 USD"
		, "unspent"));
	_tx.append(new TxObject(
		"12 June 2018 | 3:46 PM"
		, "super_user"
		, "Beam is super cool, bla bla bla..."
		, "+0.63736 BEAM"
		, "726.4 USD"
		, "unspent"));
	_tx.append(new TxObject(
		"12 June 2018 | 3:46 PM"
		, "super_user"
		, "Beam is super cool, bla bla bla..."
		, "+0.63736 BEAM"
		, "726.4 USD"
		, "unspent"));
	_tx.append(new TxObject(
		"12 June 2018 | 3:46 PM"
		, "super_user"
		, "Beam is super cool, bla bla bla..."
		, "+0.63736 BEAM"
		, "726.4 USD"
		, "unspent"));
}

namespace 
{
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

QString WalletViewModel::available() const
{
	auto amount = getAvailable(_keychain);

	return QString::number(static_cast<float>(amount) / Rules::Coin) + " BEAM";
}

//void WalletViewModel::setAvailable(const QString& val)
//{
//	if (_label != val)
//	{
//		_label = val;
//
//		emit labelChanged();
//	}
//}

//void WalletViewModel::sayHello(const QString& name)
//{
//	setLabel("Hello, " + name);
//}

//const WalletViewModel::TxList& WalletViewModel::tx() const
QVariant WalletViewModel::tx() const
{
	return QVariant::fromValue(_tx);
}
