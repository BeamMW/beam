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
#pragma once

#include "wallet_model.h"
#include "swap_coin_client_model.h"
#include "settings.h"
#include "messages.h"
#include "node_model.h"
#include "helpers.h"
#include "wallet/secstring.h"
#include "keykeeper/private_key_keeper.h"
#include "wallet/bitcoin/bridge_holder.h"
#include <memory>

class AppModel final: public QObject
{
    Q_OBJECT
public:
    static AppModel& getInstance();

    AppModel(WalletSettings& settings);
    ~AppModel() override;

    bool createWallet(const beam::SecString& seed, const beam::SecString& pass);

#if defined(BEAM_HW_WALLET)
    bool createTrezorWallet(std::shared_ptr<ECC::HKdfPub> ownerKey, const beam::SecString& pass);
#endif

    bool openWallet(const beam::SecString& pass);
    bool checkWalletPassword(const beam::SecString& pass) const;
    void changeWalletPassword(const std::string& pass);

    void applySettingsChanges();
    void nodeSettingsChanged();
    void resetWallet();

    WalletModel::Ptr getWallet() const;
    WalletSettings& getSettings() const;
    MessageManager& getMessages();
    NodeModel& getNode();
    SwapCoinClientModel::Ptr getBitcoinClient() const;
    SwapCoinClientModel::Ptr getLitecoinClient() const;
    SwapCoinClientModel::Ptr getQtumClient() const;

public slots:
    void onStartedNode();
    void onFailedToStartNode(beam::wallet::ErrorType errorCode);
    void onResetWallet();

signals:
    void walletReset();
    void walletResetCompleted();

private:
    void start();
    void startNode();
    void startWallet();
    void InitBtcClient();
    void InitLtcClient();
    void InitQtumClient();
    void onWalledOpened(const beam::SecString& pass);
    void backupDB(const std::string& dbFilePath);
    void restoreDBFromBackup(const std::string& dbFilePath);
    void generateDefaultAddress();

private:
    // SwapCoinClientModels must be destroyed after WalletModel
    SwapCoinClientModel::Ptr m_bitcoinClient;
    SwapCoinClientModel::Ptr m_litecoinClient;
    SwapCoinClientModel::Ptr m_qtumClient;

    beam::bitcoin::IBridgeHolder::Ptr m_btcBridgeHolder;
    beam::bitcoin::IBridgeHolder::Ptr m_ltcBridgeHolder;
    beam::bitcoin::IBridgeHolder::Ptr m_qtumBridgeHolder;

    WalletModel::Ptr m_wallet;
    beam::wallet::IPrivateKeyKeeper::Ptr m_keyKeeper;
    NodeModel m_nodeModel;
    WalletSettings& m_settings;
    MessageManager m_messages;
    ECC::NoLeak<ECC::uintBig> m_passwordHash;
    beam::io::Reactor::Ptr m_walletReactor;
    beam::wallet::IWalletDB::Ptr m_db;
    Connections m_nsc; // [n]ode [s]tarting [c]onnections
    Connections m_walletConnections;
    static AppModel* s_instance;
    std::string m_walletDBBackupPath;
};
