#include <QApplication>
#include <QtQuick>
#include <QInputDialog>

#include <qqmlcontext.h>

#include "viewmodel/main.h"
#include "viewmodel/dashboard.h"
#include "viewmodel/wallet.h"
#include "viewmodel/notifications.h"
#include "viewmodel/help.h"
#include "viewmodel/settings.h"

int main (int argc, char* argv[])
{
	QApplication app(argc, argv);

	bool ok = false;
	QString pass = QInputDialog::getText(0, "Password", "Please, enter wallet password:", QLineEdit::Password, nullptr, &ok);

	if (ok)
	{
		{
		}

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
	
	return 0;
}
