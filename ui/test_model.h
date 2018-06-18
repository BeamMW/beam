#pragma once

#include <QObject>

class DataObject : public QObject
{
	Q_OBJECT

	Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)

public:
	DataObject();

	QString label() const;
	void setLabel(const QString& val);

	void sayHello(const QString& name);

signals:
	void labelChanged();

private:

	QString _label;
};
