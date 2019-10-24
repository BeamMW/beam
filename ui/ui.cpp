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
#include <QQmlApplicationEngine>

#include <QInputDialog>
#include <QMessageBox>
// uncomment for QML profiling
//#include <QQmlDebuggingEnabler>
//QQmlDebuggingEnabler enabler;
#include <qqmlcontext.h>
#include "viewmodel/start_view.h"
#include "viewmodel/loading_view.h"
#include "viewmodel/main_view.h"
#include "viewmodel/utxo_view.h"
#include "viewmodel/utxo_view_status.h"
#include "viewmodel/utxo_view_type.h"
#include "viewmodel/atomic_swap/swap_offers_view.h"
#include "viewmodel/dashboard_view.h"
#include "viewmodel/address_book_view.h"
#include "viewmodel/wallet/wallet_view.h"
#include "viewmodel/notifications_view.h"
#include "viewmodel/help_view.h"
#include "viewmodel/settings_view.h"
#include "viewmodel/messages_view.h"
#include "viewmodel/statusbar_view.h"
#include "viewmodel/theme.h"
#include "viewmodel/receive_view.h"
#include "viewmodel/receive_swap_view.h"
#include "viewmodel/send_view.h"
#include "viewmodel/send_swap_view.h"
#include "viewmodel/el_seed_validator.h"
#include "viewmodel/currencies.h"
#include "model/app_model.h"
#include "viewmodel/qml_globals.h"
#include "viewmodel/helpers/list_model.h"
#include "viewmodel/helpers/sortfilterproxymodel.h"
#include "viewmodel/helpers/token_bootstrap_manager.h"
#include "wallet/wallet_db.h"
#include "utility/log_rotation.h"
#include "core/ecc_native.h"
#include "utility/cli/options.h"
#include <QtCore/QtPlugin>
#include "version.h"
#include "utility/string_helpers.h"
#include "utility/helpers.h"
#include "model/translator.h"

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

#ifdef APP_NAME
static const char* AppName = APP_NAME;
#else
static const char* AppName = "Beam Wallet Masternet";
#endif

int main (int argc, char* argv[])
{
#if defined Q_OS_WIN
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    block_sigpipe();

    QApplication app(argc, argv);

	app.setWindowIcon(QIcon(Theme::iconPath()));

    QApplication::setApplicationName(AppName);

    QDir appDataDir(QStandardPaths::writableLocation(QStandardPaths::DataLocation));

    try
    {

        // TODO: ugly temporary fix for unused variable, GCC only
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

        auto [options, visibleOptions] = createOptionsDescription(GENERAL_OPTIONS | UI_OPTIONS | WALLET_OPTIONS);

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

        po::variables_map vm;

        try
        {
            vm = getOptions(argc, argv, WalletSettings::WalletCfg, options, true);
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

#define LOG_FILES_PREFIX "beam_ui_"

        const auto logFilesPath = appDataDir.filePath(WalletSettings::LogsFolder).toStdString();
        auto logger = beam::Logger::create(logLevel, logLevel, fileLogLevel, LOG_FILES_PREFIX, logFilesPath);

        unsigned logCleanupPeriod = vm[cli::LOG_CLEANUP_DAYS].as<uint32_t>() * 24 * 3600;

        clean_old_logfiles(logFilesPath, LOG_FILES_PREFIX, logCleanupPeriod);

        try
        {
            Rules::get().UpdateChecksum();
            LOG_INFO() << "Beam Wallet UI " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
            LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();

            // AppModel Model MUST BE created before the UI engine and destroyed after.
            // AppModel serves the UI and UI should be able to access AppModel at any time
            // even while being destroyed. Do not move engine above AppModel
            WalletSettings settings(appDataDir);
            AppModel appModel(settings);
            QQmlApplicationEngine engine;
            Translator translator(settings, engine);
            
            if (settings.getNodeAddress().isEmpty())
            {
                if (vm.count(cli::NODE_ADDR))
                {
                    string nodeAddr = vm[cli::NODE_ADDR].as<string>();
                    settings.setNodeAddress(nodeAddr.c_str());
                }
            }

            qmlRegisterSingletonType<Theme>(
                    "Beam.Wallet", 1, 0, "Theme",
                    [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
                        Q_UNUSED(engine)
                        Q_UNUSED(scriptEngine)
                        return new Theme;
                    });

            qmlRegisterSingletonType<QMLGlobals>(
                    "Beam.Wallet", 1, 0, "BeamGlobals",
                    [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
                        Q_UNUSED(engine)
                        Q_UNUSED(scriptEngine)
                        return new QMLGlobals(*engine);
                    });

            qmlRegisterType<WalletCurrency>("Beam.Wallet", 1, 0, "Currency");
            qmlRegisterType<StartViewModel>("Beam.Wallet", 1, 0, "StartViewModel");
            qmlRegisterType<LoadingViewModel>("Beam.Wallet", 1, 0, "LoadingViewModel");
            qmlRegisterType<MainViewModel>("Beam.Wallet", 1, 0, "MainViewModel");
            qmlRegisterType<DashboardViewModel>("Beam.Wallet", 1, 0, "DashboardViewModel");
            qmlRegisterType<WalletViewModel>("Beam.Wallet", 1, 0, "WalletViewModel");
            qmlRegisterType<UtxoViewStatus>("Beam.Wallet", 1, 0, "UtxoStatus");
            qmlRegisterType<UtxoViewType>("Beam.Wallet", 1, 0, "UtxoType");
            qmlRegisterType<UtxoViewModel>("Beam.Wallet", 1, 0, "UtxoViewModel");
            qmlRegisterType<SettingsViewModel>("Beam.Wallet", 1, 0, "SettingsViewModel");
            qmlRegisterType<AddressBookViewModel>("Beam.Wallet", 1, 0, "AddressBookViewModel");
            qmlRegisterType<SwapOffersViewModel>("Beam.Wallet", 1, 0, "SwapOffersViewModel");
            qmlRegisterType<NotificationsViewModel>("Beam.Wallet", 1, 0, "NotificationsViewModel");
            qmlRegisterType<HelpViewModel>("Beam.Wallet", 1, 0, "HelpViewModel");
            qmlRegisterType<MessagesViewModel>("Beam.Wallet", 1, 0, "MessagesViewModel");
            qmlRegisterType<StatusbarViewModel>("Beam.Wallet", 1, 0, "StatusbarViewModel");
            qmlRegisterType<ReceiveViewModel>("Beam.Wallet", 1, 0, "ReceiveViewModel");
            qmlRegisterType<ReceiveSwapViewModel>("Beam.Wallet", 1, 0, "ReceiveSwapViewModel");
            qmlRegisterType<SendViewModel>("Beam.Wallet", 1, 0, "SendViewModel");
            qmlRegisterType<SendSwapViewModel>("Beam.Wallet", 1, 0, "SendSwapViewModel");
            qmlRegisterType<ELSeedValidator>("Beam.Wallet", 1, 0, "ELSeedValidator");

            qmlRegisterType<AddressItem>("Beam.Wallet", 1, 0, "AddressItem");
            qmlRegisterType<ContactItem>("Beam.Wallet", 1, 0, "ContactItem");
            qmlRegisterType<UtxoItem>("Beam.Wallet", 1, 0, "UtxoItem");
            qmlRegisterType<PaymentInfoItem>("Beam.Wallet", 1, 0, "PaymentInfoItem");
            qmlRegisterType<WalletDBPathItem>("Beam.Wallet", 1, 0, "WalletDBPathItem");
            qmlRegisterType<SwapOfferItem>("Beam.Wallet", 1, 0, "SwapOfferItem");
            qmlRegisterType<SwapOffersList>("Beam.Wallet", 1, 0, "SwapOffersList");
            qmlRegisterType<TokenBootstrapManager>("Beam.Wallet", 1, 0, "TokenBootstrapManager");
            
            qmlRegisterType<SortFilterProxyModel>("Beam.Wallet", 1, 0, "SortFilterProxyModel");

            engine.load(QUrl("qrc:/root.qml"));

            if (engine.rootObjects().count() < 1)
            {
                LOG_ERROR() << "Probmlem with QT";
                return -1;
            }

            QObject* topLevel = engine.rootObjects().value(0);
            QQuickWindow* window = qobject_cast<QQuickWindow*>(topLevel);

            if (!window)
            {
                LOG_ERROR() << "Probmlem with QT";
                return -1;
            }

            //window->setMinimumSize(QSize(768, 540));
            window->setFlag(Qt::WindowFullscreenButtonHint);
            window->show();

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
