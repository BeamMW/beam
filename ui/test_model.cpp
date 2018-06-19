#include "test_model.h"

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

DataObject::DataObject()
	: _label("Please, click the button!")
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

QString DataObject::label() const
{
	return _label;
}

void DataObject::setLabel(const QString& val)
{
	if (_label != val)
	{
		_label = val;

		emit labelChanged();
	}
}

void DataObject::sayHello(const QString& name)
{
	setLabel("Hello, " + name);
}

const DataObject::TxList& DataObject::tx() const
{
	return _tx;
}
