#include <QApplication>
#include <QtQuick>
#include <QMessageBox>
#include <QInputDialog>

#include <qqmlcontext.h>

#include "viewmodel/main.h"
#include "viewmodel/dashboard.h"
#include "viewmodel/wallet.h"
#include "viewmodel/notifications.h"
#include "viewmodel/help.h"
#include "viewmodel/settings.h"

#include "wallet/wallet_db.h"
#include "utility/logger.h"

#include "translator.h"

using namespace beam;
using namespace ECC;

int main (int argc, char* argv[])
{
	auto logger = Logger::create();

	QApplication app(argc, argv);

	static const char* WALLET_STORAGE = "wallet.db";
	QString pass;

	if (!Keychain::isInitialized(WALLET_STORAGE))
	{
		if (QMessageBox::warning(0, "Warning", "Your wallet isn't created. Do you want to create it?", QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
		{
			bool ok = false;
			pass = QInputDialog::getText(0, "Password", "Please, enter a password for your wallet:", QLineEdit::Password, nullptr, &ok);

			if (ok)
			{
				if (!pass.isEmpty())
				{
					NoLeak<uintBig> walletSeed;
					walletSeed.V = Zero;
					{
						// TODO: temporary solution
						// read it from the config
						const char* seed = "123";
						Hash::Value hv;
						Hash::Processor() << seed >> hv;
						walletSeed.V = hv;
					}

					auto keychain = Keychain::init(WALLET_STORAGE, pass.toStdString(), walletSeed);

					if (!keychain)
					{
						QMessageBox::critical(0, "Error", "Your wallet isn't created. Something went wrong.", QMessageBox::Ok);
						return 0;
					}
				}
				else
				{
					QMessageBox::critical(0, "Error", "Your wallet isn't created. Please, provide password for the wallet.", QMessageBox::Ok);
					return 0;
				}
			}
		}
		else return 0;
	}

	if (pass.isEmpty())
	{
		bool ok = false;
		pass = QInputDialog::getText(0, "Password", "Please, enter a password for your wallet:", QLineEdit::Password, nullptr, &ok);

		if (!ok)
		{
			return 0;
		}
	}

	{
		auto keychain = Keychain::open(WALLET_STORAGE, pass.toStdString());

		if (keychain)
		{
			struct ViewModel
			{
				MainViewModel			main;
				DashboardViewModel		dashboard;
				WalletViewModel			wallet;
				NotificationsViewModel	notifications;
				HelpViewModel			help;
				SettingsViewModel		settings;

				ViewModel(IKeyChain::Ptr keychain) : wallet(keychain) {}

			} viewModel(keychain);

			Translator translator;

			QQuickView view;
			view.setResizeMode(QQuickView::SizeRootObjectToView);

			QQmlContext *ctxt = view.rootContext();

			ctxt->setContextProperty("mainViewModel", &viewModel.main);

			ctxt->setContextProperty("walletViewModel", &viewModel.wallet);

			ctxt->setContextProperty("translator", &translator);

			view.setSource(QUrl("qrc:///main.qml"));
			view.show();

			return app.exec();
		}
		else
		{
			QMessageBox::critical(0, "Error", "Wallet data unreadable, restore wallet.db from latest backup or delete it and reinitialize the wallet.", QMessageBox::Ok);
			return 0;
		}
	}

	return 0;
}
