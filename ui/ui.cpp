#include <QApplication>
#include <QtQuick>

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

#include <boost/program_options.hpp>

namespace po = boost::program_options;

using namespace beam;
using namespace std;
using namespace ECC;

// TODO: use programm options from beam.cpp
namespace cli
{
	const char* PORT = "port";
	const char* WALLET_PASS = "pass";
	const char* NODE_ADDR = "node_addr";
	const char* FAKE_POW = "FakePoW";
}

int main (int argc, char* argv[])
{
	auto logger = Logger::create();

	try
	{
		po::options_description rules("UI options");
		rules.add_options()
			(cli::PORT, po::value<uint16_t>())
			(cli::WALLET_PASS, po::value<string>())
			(cli::NODE_ADDR, po::value<string>())
			(cli::FAKE_POW, po::value<bool>()->default_value(Rules::FakePoW));

		po::options_description options{ "Allowed options" };
		options.add(rules);

		po::variables_map vm;

		{
			static const char* WalletDB = "beam-ui.cfg";

			std::ifstream cfg(WalletDB);

			if (cfg)
			{
				po::store(po::parse_config_file(cfg, options), vm);
			}
			else
			{
				LOG_ERROR() << WalletDB << " not found!";
				return -1;
			}
		}

		QApplication app(argc, argv);

		string pass;
		if (vm.count(cli::WALLET_PASS))
		{
			pass = vm[cli::WALLET_PASS].as<string>();
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

		Rules::FakePoW = vm[cli::FAKE_POW].as<bool>();

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
				struct ViewModel
				{
					MainViewModel			main;
					DashboardViewModel		dashboard;
					WalletViewModel			wallet;
					NotificationsViewModel	notifications;
					HelpViewModel			help;
					SettingsViewModel		settings;

					ViewModel(IKeyChain::Ptr keychain, uint16_t port, const string& nodeAddr) : wallet(keychain, port, nodeAddr) {}

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

	return 0;
}
