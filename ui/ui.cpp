#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickWindow>
#include <QtQuick>

#include <qqmlcontext.h>

#include "viewmodel/wallet_model.h"

int main (int argc, char* argv[])
{
	QGuiApplication app(argc, argv);

	WalletViewModel wallet;

	QQuickView view;
	view.setResizeMode(QQuickView::SizeRootObjectToView);

	QQmlContext *ctxt = view.rootContext();
	ctxt->setContextProperty("model", &wallet);

	ctxt->setContextProperty("listModel", QVariant::fromValue(wallet.tx()));

	view.setSource(QUrl("qrc:///wallet.qml"));
	view.show();

    return app.exec();
}
