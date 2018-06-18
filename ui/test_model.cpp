#include "test_model.h"

DataObject::DataObject()
: _label("Please, click the button!")
{

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
