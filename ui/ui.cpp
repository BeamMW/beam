#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickWindow>
#include <QtQuick>

#include <qqmlcontext.h>

#include "viewmodel/main.h"
#include "viewmodel/dashboard.h"
#include "viewmodel/wallet.h"
#include "viewmodel/notifications.h"
#include "viewmodel/help.h"
#include "viewmodel/settings.h"

int main (int argc, char* argv[])
{
	QGuiApplication app(argc, argv);

	struct
	{
		MainViewModel main;
		DashboardViewModel dashboard;
		WalletViewModel wallet;
		NotificationsViewModel notifications;
		HelpViewModel help;
		SettingsViewModel settings;
	} viewModel;

	QQuickView view;
	view.setResizeMode(QQuickView::SizeRootObjectToView);

	QQmlContext *ctxt = view.rootContext();

	ctxt->setContextProperty("mainViewModel", &viewModel.main);

	ctxt->setContextProperty("walletViewModel", &viewModel.wallet);
	ctxt->setContextProperty("listModel", QVariant::fromValue(viewModel.wallet.tx()));

	view.setSource(QUrl("qrc:///main.qml"));
	view.show();

    return app.exec();
}
