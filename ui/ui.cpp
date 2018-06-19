#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickWindow>
#include <QtQuick>
#include <QQmlContext.h>

#include <QTimer>
#include <QObject>

#include "test_model.h"

int main (int argc, char* argv[])
{
	QGuiApplication app(argc, argv);

	DataObject data;

	QQuickView view;
	view.setResizeMode(QQuickView::SizeRootObjectToView);

	QQmlContext *ctxt = view.rootContext();
	ctxt->setContextProperty("model", &data);

	ctxt->setContextProperty("listModel", QVariant::fromValue(data.tx()));

	view.setSource(QUrl::fromLocalFile("qml/hw.qml"));
	view.show();

    return app.exec();
}
