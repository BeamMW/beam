#include <QApplication>
#include <QtQuick>

#include <qqmlcontext.h>
#include "viewmodel/main.h"
#include "viewmodel/dashboard.h"
#include "viewmodel/address_book.h"
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

#include <boost/filesystem.hpp>

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
	try
	{
		po::options_description options = createOptionsDescription();
		po::variables_map vm;
		try
		{
			vm = getOptions(argc, argv, "beam.cfg", options);
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

		int logLevel = getLogLevel(cli::LOG_LEVEL, vm, LOG_LEVEL_DEBUG);
		int fileLogLevel = getLogLevel(cli::FILE_LOG_LEVEL, vm, LOG_LEVEL_INFO);
#if LOG_VERBOSE_ENABLED
		logLevel = LOG_LEVEL_VERBOSE;
#endif
		
		const auto path = boost::filesystem::system_complete("./logs");
		auto logger = beam::Logger::create(logLevel, logLevel, fileLogLevel, "beam_ui_", path.string());

		try
		{
			po::options_description options = createOptionsDescription();

			po::variables_map vm = getOptions(argc, argv, "beam-ui.cfg", options);

			if (vm.count(cli::HELP))
			{
				cout << options << std::endl;
			}

			//if (vm.count(cli::NODE_PEER))
			//{
			//	auto peers = vm[cli::NODE_PEER].as<vector<string>>();
			//}

			QApplication app(argc, argv);

			string pass;
			if (vm.count(cli::PASS))
			{
				pass = vm[cli::PASS].as<string>();
			}
			else
			{
				LOG_ERROR() << "Please, provide wallet password!";
				return -1;
			}

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

			static const char* WALLET_STORAGE = "wallet.db";
			if (!Keychain::isInitialized(WALLET_STORAGE))
			{
				LOG_ERROR() << WALLET_STORAGE << " not found!";
				return -1;
			}

			{
				auto keychain = Keychain::open(WALLET_STORAGE, pass);

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

					WalletModel model(keychain, vm[cli::PORT].as<uint16_t>(), vm[cli::NODE_ADDR].as<string>());

					model.start();

					struct ViewModel
					{
						MainViewModel			main;
						DashboardViewModel		dashboard;
						WalletViewModel			wallet;
                        AddressBookViewModel    addressBook;
						NotificationsViewModel	notifications;
						HelpViewModel			help;
						SettingsViewModel		settings;

						ViewModel(WalletModel& model)
							: wallet(model)
                            , addressBook(model)
                        {
                        }

					} viewModel(model);

					Translator translator;

					QQuickView view;
					view.setResizeMode(QQuickView::SizeRootObjectToView);

					QQmlContext *ctxt = view.rootContext();

                    // TODO: try move instantiation of view models to views
					ctxt->setContextProperty("mainViewModel", &viewModel.main);

					ctxt->setContextProperty("walletViewModel", &viewModel.wallet);
                    ctxt->setContextProperty("addressBookViewModel", &viewModel.addressBook);

					ctxt->setContextProperty("translator", &translator);

					view.setSource(QUrl("qrc:///main.qml"));
					view.show();

					return app.exec();
				}
				else
				{
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
