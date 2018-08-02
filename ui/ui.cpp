#include <QApplication>
#include <QtQuick>

#include <QInputDialog>
#include <QMessageBox>

#include <qqmlcontext.h>
#include "viewmodel/main.h"
#include "viewmodel/dashboard.h"
#include "viewmodel/wallet.h"
#include "viewmodel/notifications.h"
#include "viewmodel/help.h"
#include "viewmodel/settings.h"

#include "wallet/wallet_db.h"
#include "utility/logger.h"
#include "core/ecc_native.h"

#include "translator.h"

#include "utility/options.h"

#include <QtCore/QtPlugin>

#include "version.h"

#if defined(BEAM_USE_STATIC)

#if defined Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#elif defined Q_OS_MAC
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#elif defined Q_OS_LINUX
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
#endif

Q_IMPORT_PLUGIN(QtQuick2Plugin)
Q_IMPORT_PLUGIN(QtQuick2WindowPlugin)
Q_IMPORT_PLUGIN(QtQuickControls1Plugin)
Q_IMPORT_PLUGIN(QtQuickControls2Plugin)
Q_IMPORT_PLUGIN(QtGraphicalEffectsPlugin)
Q_IMPORT_PLUGIN(QtGraphicalEffectsPrivatePlugin)
Q_IMPORT_PLUGIN(QSvgPlugin)
Q_IMPORT_PLUGIN(QtQuickLayoutsPlugin)
Q_IMPORT_PLUGIN(QtQuickTemplates2Plugin)

#endif

using namespace beam;
using namespace std;
using namespace ECC;

namespace
{
	template<typename Out>
	void split(const string &s, char delim, Out result) {
		stringstream ss(s);
		string item;
		while (getline(ss, item, delim)) {
			*(result++) = item;
		}
	}

	vector<string> split(const string &s, char delim) {
		vector<string> elems;
		split(s, delim, back_inserter(elems));
		return elems;
	}
}

int main (int argc, char* argv[])
{
	QApplication::setApplicationName("Beam");
	QApplication::setOrganizationName("beam-mw.com");

	QDir appDataDir(QStandardPaths::writableLocation(QStandardPaths::DataLocation));

	try
	{
		po::options_description options = createOptionsDescription();
		po::variables_map vm;
				
		try
		{
			vm = getOptions(argc, argv, "beam-ui.cfg", options);
		}
		catch (const po::error& e)
		{
			cout << e.what() << std::endl;
			cout << options << std::endl;

			return -1;
		}

		if (vm.count(cli::HELP))
		{
			cout << options << std::endl;

			return 0;
		}

		if (vm.count(cli::VERSION))
		{
			cout << PROJECT_VERSION << endl;
			return 0;
		}

		if (vm.count(cli::GIT_COMMIT_HASH))
		{
			cout << GIT_COMMIT_HASH << endl;
			return 0;
		}


		int logLevel = getLogLevel(cli::LOG_LEVEL, vm, LOG_LEVEL_DEBUG);
		int fileLogLevel = getLogLevel(cli::FILE_LOG_LEVEL, vm, LOG_LEVEL_INFO);
#if LOG_VERBOSE_ENABLED
		logLevel = LOG_LEVEL_VERBOSE;
#endif
		
		auto logger = beam::Logger::create(logLevel, logLevel, fileLogLevel, "beam_ui_", appDataDir.filePath("./logs").toStdString());

		try
		{
			QApplication app(argc, argv);

			if (!vm.count(cli::NODE_ADDR))
			{
				LOG_ERROR() << "Please, provide node address!";
				return -1;
			}

			if (!vm.count(cli::PORT))
			{
				LOG_ERROR() << "Please, provide port!";
				return -1;
			}

			Rules::get().UpdateChecksum();
			LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

			auto walletStorage = appDataDir.filePath("wallet.db").toStdString();
			std::string walletPass;

			if (!Keychain::isInitialized(walletStorage))
			{
				bool ok = true;
				QString seed;

				while (seed.isEmpty() && ok)
				{
					seed = QInputDialog::getText(0, "Beam", "wallet.db not found\nPlease, enter a seed to initialize your wallet:", QLineEdit::Normal, nullptr, &ok);
				}

				if (ok && !seed.isEmpty())
				{
					QString pass;

					while (pass.isEmpty() && ok)
					{
						pass = QInputDialog::getText(0, "Beam", "Please, enter a password:", QLineEdit::Password, nullptr, &ok);
					}

					if (ok && !pass.isEmpty()) 
					{
						walletPass = pass.toStdString();

						NoLeak<uintBig> walletSeed;
						walletSeed.V = Zero;
						{
							Hash::Value hv;
							Hash::Processor() << seed.toStdString().c_str() >> hv;
							walletSeed.V = hv;
						}

						auto keychain = Keychain::init(walletStorage, walletPass, walletSeed);

						if (keychain)
						{
							QMessageBox::information(0, "Beam", "wallet.db successfully created.", QMessageBox::Ok);
						}
						else
						{
							QMessageBox::critical(0, "Error", "Your wallet isn't created. Something went wrong.", QMessageBox::Ok);
							return -1;
						}
					}
					else return 0;
				}
			}
			else
			{
				bool ok = true;
				QString pass;

				while (pass.isEmpty() && ok)
				{
					pass = QInputDialog::getText(0, "Beam", "Please, enter a password:", QLineEdit::Password, nullptr, &ok);
				}

				if (ok && !pass.isEmpty())
				{
					walletPass = pass.toStdString();
				}
				else return 0;
			}

			{
				auto keychain = Keychain::open(walletStorage, walletPass);

				if (keychain)
				{
					// delete old peers before importing new from .cfg
					keychain->clearPeers();

					if (vm.count(cli::WALLET_ADDR))
					{
						auto uris = vm[cli::WALLET_ADDR].as<vector<string>>();

						for (const auto& uri : uris)
						{
							auto vars = split(uri, '&');

							beam::TxPeer addr;

							for (const auto& var : vars)
							{
								auto parts = split(var, '=');

								assert(parts.size() == 2);

								auto varName = parts[0];
								auto varValue = parts[1];

								if (varName == "label") addr.m_label = varValue;
								else if (varName == "ip")
								{
									addr.m_address = varValue;
								}
								else if (varName == "hash")
								{
									ECC::Hash::Processor hp;
									hp << varValue.c_str() >> addr.m_walletID;
								}
								else assert(!"Unknown variable");
							}
							keychain->addPeer(addr);
						}
					}

					struct ViewModel
					{
						MainViewModel			main;
						DashboardViewModel		dashboard;
						WalletViewModel			wallet;
						NotificationsViewModel	notifications;
						HelpViewModel			help;
						SettingsViewModel		settings;

						ViewModel(IKeyChain::Ptr keychain, uint16_t port, const string& nodeAddr)
							: wallet(keychain, port, nodeAddr) {}

					} viewModel(keychain, vm[cli::PORT].as<uint16_t>(), vm[cli::NODE_ADDR].as<string>());

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
					QMessageBox::critical(0, "Error", "Invalid password or wallet data unreadable.\nRestore wallet.db from latest backup or delete it and reinitialize the wallet.", QMessageBox::Ok);

					LOG_ERROR() << "Wallet data unreadable, restore wallet.db from latest backup or delete it and reinitialize the wallet.";
					return -1;
				}
			}

		}
		catch (const po::error& e)
		{
			LOG_ERROR() << e.what();
			return -1;
		}
	}
	catch (const std::exception& e)
	{
		std::cout << e.what() << std::endl;
		return -1;
	}
}
