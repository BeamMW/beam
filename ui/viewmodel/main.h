#pragma once

#include <QObject>

class MainViewModel : public QObject
{
	Q_OBJECT

public slots:
	void update(int page);
};
