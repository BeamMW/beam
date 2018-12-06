// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <QApplication>
#include <QtQuick>

#include <QInputDialog>
#include <QMessageBox>

#include <qqmlcontext.h>
#include "viewmodel/start_view.h"
#include "viewmodel/restore_view.h"
#include "viewmodel/main_view.h"
#include "viewmodel/utxo_view.h"
#include "viewmodel/dashboard_view.h"
#include "viewmodel/address_book_view.h"
#include "viewmodel/wallet_view.h"
#include "viewmodel/notifications_view.h"
#include "viewmodel/help_view.h"
#include "viewmodel/settings_view.h"
#include "viewmodel/messages_view.h"
#include "viewmodel/statusbar_view.h"
#include "model/app_model.h"

#include "wallet/wallet_db.h"
#include "utility/logger.h"
#include "core/ecc_native.h"

#include "translator.h"

#include "utility/options.h"

#include <QtCore/QtPlugin>

#include "version.h"

#include "utility/string_helpers.h"
#include "utility/helpers.h"

#if defined(BEAM_USE_STATIC)

#if defined Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsPrinterSupportPlugin)
#elif defined Q_OS_MAC
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
Q_IMPORT_PLUGIN(QCocoaPrinterSupportPlugin)
#elif defined Q_OS_LINUX
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
Q_IMPORT_PLUGIN(QXcbGlxIntegrationPlugin)
Q_IMPORT_PLUGIN(QCupsPrinterSupportPlugin)
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

static const char* AppName = "Beam Wallet";

int main (int argc, char* argv[])
{
#if defined Q_OS_WIN
	QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    block_sigpipe();

    QApplication app(argc, argv);

	app.setWindowIcon(QIcon(":/assets/icon.png"));

    QApplication::setApplicationName(AppName);

    QDir appDataDir(QStandardPaths::writableLocation(QStandardPaths::DataLocation));

    try
    {
        po::options_description options = createOptionsDescription(GENERAL_OPTIONS | UI_OPTIONS | WALLET_OPTIONS);
        po::variables_map vm;

        try
        {
            vm = getOptions(argc, argv, WalletSettings::WalletCfg, options);
        }
        catch (const po::error& e)
        {
            QMessageBox msgBox;
            msgBox.setText(e.what());
            msgBox.exec();
            return -1;
        }

        if (vm.count(cli::VERSION))
        {
            QMessageBox msgBox;
            msgBox.setText(PROJECT_VERSION.c_str());
            msgBox.exec();
            return 0;
        }

        if (vm.count(cli::GIT_COMMIT_HASH))
        {
            QMessageBox msgBox;
            msgBox.setText(GIT_COMMIT_HASH.c_str());
            msgBox.exec();
            return 0;
        }

        if (vm.count(cli::APPDATA_PATH))
        {
            appDataDir = QString::fromStdString(vm[cli::APPDATA_PATH].as<string>());
        }

        int logLevel = getLogLevel(cli::LOG_LEVEL, vm, LOG_LEVEL_DEBUG);
        int fileLogLevel = getLogLevel(cli::FILE_LOG_LEVEL, vm, LOG_LEVEL_DEBUG);

        beam::Crash::InstallHandler(appDataDir.filePath(AppName).toStdString().c_str());

        auto logger = beam::Logger::create(logLevel, logLevel, fileLogLevel, "beam_ui_",
			appDataDir.filePath(WalletSettings::LogsFolder).toStdString());

        try
        {
            Rules::get().UpdateChecksum();
            LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

            QQuickView view;
            view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.setMinimumSize(QSize(780, 500));
            view.setFlag(Qt::WindowFullscreenButtonHint);
            WalletSettings settings(appDataDir);
            AppModel appModel(settings);

            if (settings.getNodeAddress().isEmpty())
            {
                if (vm.count(cli::NODE_ADDR))
                {
                    string nodeAddr = vm[cli::NODE_ADDR].as<string>();
                    settings.setNodeAddress(nodeAddr.c_str());
                }
            }

            qmlRegisterType<StartViewModel>("Beam.Wallet", 1, 0, "StartViewModel");
            qmlRegisterType<RestoreViewModel>("Beam.Wallet", 1, 0, "RestoreViewModel");
            qmlRegisterType<MainViewModel>("Beam.Wallet", 1, 0, "MainViewModel");
            qmlRegisterType<DashboardViewModel>("Beam.Wallet", 1, 0, "DashboardViewModel");
            qmlRegisterType<WalletViewModel>("Beam.Wallet", 1, 0, "WalletViewModel");
            qmlRegisterType<UtxoViewModel>("Beam.Wallet", 1, 0, "UtxoViewModel");
            qmlRegisterType<SettingsViewModel>("Beam.Wallet", 1, 0, "SettingsViewModel");
            qmlRegisterType<AddressBookViewModel>("Beam.Wallet", 1, 0, "AddressBookViewModel");
            qmlRegisterType<NotificationsViewModel>("Beam.Wallet", 1, 0, "NotificationsViewModel");
            qmlRegisterType<HelpViewModel>("Beam.Wallet", 1, 0, "HelpViewModel");
            qmlRegisterType<MessagesViewModel>("Beam.Wallet", 1, 0, "MessagesViewModel");
            qmlRegisterType<StatusbarViewModel>("Beam.Wallet", 1, 0, "StatusbarViewModel");

            qmlRegisterType<AddressItem>("Beam.Wallet", 1, 0, "AddressItem");
            qmlRegisterType<ContactItem>("Beam.Wallet", 1, 0, "ContactItem");
            qmlRegisterType<TxObject>("Beam.Wallet", 1, 0, "TxObject");
            qmlRegisterType<UtxoItem>("Beam.Wallet", 1, 0, "UtxoItem");

            Translator translator;
            view.setSource(QUrl("qrc:/root.qml"));

            view.show();

            return app.exec();
        }
        catch (const po::error& e)
        {
            LOG_ERROR() << e.what();
            return -1;
        }
    }
    catch (const std::exception& e)
    {
        QMessageBox msgBox;
        msgBox.setText(e.what());
        msgBox.exec();
        return -1;
    }
}
