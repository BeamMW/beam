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
#include "core/ecc_native.h"

#include "translator.h"

#include <boost/program_options.hpp>

namespace po = boost::program_options;

using namespace beam;
using namespace std;
using namespace ECC;

// TODO: use programm options from beam.cpp
namespace ui
{
	const char* PORT = "port";
	const char* WALLET_PASS = "pass";
	const char* NODE_ADDR = "node_addr";
	const char* FAKE_POW = "FakePoW";
	const char* NODE_PEER = "peer";
	const char* WALLET_ADDR = "addr";
}

namespace
{
	template<typename Out>
	void split(const std::string &s, char delim, Out result) {
		std::stringstream ss(s);
		std::string item;
		while (std::getline(ss, item, delim)) {
			*(result++) = item;
		}
	}

	std::vector<std::string> split(const std::string &s, char delim) {
		std::vector<std::string> elems;
		split(s, delim, std::back_inserter(elems));
		return elems;
	}
}

int main (int argc, char* argv[])
{
	auto logger = Logger::create();

	try
	{
		po::options_description rules("UI options");
		rules.add_options()
			(ui::PORT, po::value<uint16_t>())
			(ui::WALLET_PASS, po::value<string>())
			(ui::NODE_ADDR, po::value<string>())
			(ui::FAKE_POW, po::value<bool>()->default_value(Rules::get().FakePoW))
			(ui::NODE_PEER, po::value<vector<string>>()->multitoken())
			(ui::WALLET_ADDR, po::value<vector<string>>()->multitoken());

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

		//if (vm.count(ui::NODE_PEER))
		//{
		//	auto peers = vm[ui::NODE_PEER].as<std::vector<std::string>>();
		//}

		QApplication app(argc, argv);

		string pass;
		if (vm.count(ui::WALLET_PASS))
		{
			pass = vm[ui::WALLET_PASS].as<string>();
		}
		else
		{
			LOG_ERROR() << "Please, provide wallet password!";
			return -1;
		}

		if (!vm.count(ui::NODE_ADDR))
		{
			LOG_ERROR() << "Please, provide node address!";
			return -1;
		}

		if (!vm.count(ui::PORT))
		{
			LOG_ERROR() << "Please, provide port!";
			return -1;
		}

		Rules::get().FakePoW = vm[ui::FAKE_POW].as<bool>();

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
				if (vm.count(ui::WALLET_ADDR))
				{
					auto uris = vm[ui::WALLET_ADDR].as<std::vector<std::string>>();
					AddrList addrList;

					for (auto& const uri : uris)
					{
						auto vars = split(uri, '&');

						beam::TxPeer addr;

						for (auto& const var : vars)
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

				} viewModel(keychain, vm[ui::PORT].as<uint16_t>(), vm[ui::NODE_ADDR].as<string>());

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
