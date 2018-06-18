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

	QQuickView view(QUrl::fromLocalFile("qml/hw.qml"));
	view.setResizeMode(QQuickView::SizeRootObjectToView);

	QQmlContext *ctxt = view.rootContext();
	ctxt->setContextProperty("model", &data);

	view.show();

    return app.exec();
}
