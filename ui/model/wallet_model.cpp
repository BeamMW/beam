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

#include "wallet_model.h"
#include "app_model.h"
#include "utility/logger.h"
#include "utility/bridge.h"
#include "utility/io/asyncevent.h"
#include "utility/helpers.h"

using namespace beam;
using namespace beam::io;
using namespace std;

WalletModel::WalletModel(IWalletDB::Ptr walletDB, const std::string& nodeAddr)
    : WalletClient(walletDB, nodeAddr)
{
    qRegisterMetaType<WalletStatus>("WalletStatus");
    qRegisterMetaType<ChangeAction>("beam::ChangeAction");
    qRegisterMetaType<vector<TxDescription>>("std::vector<beam::TxDescription>");
    qRegisterMetaType<Amount>("beam::Amount");
    qRegisterMetaType<vector<Coin>>("std::vector<beam::Coin>");
    qRegisterMetaType<vector<WalletAddress>>("std::vector<beam::WalletAddress>");
    qRegisterMetaType<WalletID>("beam::WalletID");
    qRegisterMetaType<WalletAddress>("beam::WalletAddress");
    qRegisterMetaType<beam::wallet::ErrorType>("beam::wallet::ErrorType");
    qRegisterMetaType<beam::TxID>("beam::TxID");
}

WalletModel::~WalletModel()
{

}

QString WalletModel::GetErrorString(beam::wallet::ErrorType type)
{
    // TODO: add more detailed error description
    switch (type)
    {
    case wallet::ErrorType::NodeProtocolBase:
        return tr("Node protocol error!");
    case wallet::ErrorType::NodeProtocolIncompatible:
        return tr("You are trying to connect to incompatible peer.");
    case wallet::ErrorType::ConnectionBase:
        return tr("Connection error.");
    case wallet::ErrorType::ConnectionTimedOut:
        return tr("Connection timed out.");
    case wallet::ErrorType::ConnectionRefused:
        return tr("Cannot connect to node: ") + getNodeAddress().c_str();
    case wallet::ErrorType::ConnectionHostUnreach:
        return tr("Node is unreachable: ") + getNodeAddress().c_str();
    case wallet::ErrorType::ConnectionAddrInUse:
    {
        auto localNodePort = AppModel::getInstance()->getSettings().getLocalNodePort();
        return QString(tr("The port %1 is already in use. Check if a wallet is already running on this machine or change the port settings.")).arg(QString::number(localNodePort));
    }
    case wallet::ErrorType::TimeOutOfSync:
        return tr("System time not synchronized.");
    default:
        return tr("Unexpected error!");
    }
}

void WalletModel::onStatus(const WalletStatus& status)
{
    emit walletStatus(status);
}

void WalletModel::onTxStatus(beam::ChangeAction action, const std::vector<beam::TxDescription>& items)
{
    emit txStatus(action, items);
}

void WalletModel::onSyncProgressUpdated(int done, int total)
{
    emit syncProgressUpdated(done, total);
}

void WalletModel::onChangeCalculated(beam::Amount change)
{
    emit changeCalculated(change);
}

void WalletModel::onAllUtxoChanged(const std::vector<beam::Coin>& utxos)
{
    emit allUtxoChanged(utxos);
}

void WalletModel::onAddresses(bool own, const std::vector<beam::WalletAddress>& addrs)
{
    emit adrresses(own, addrs);
}

void WalletModel::onGeneratedNewAddress(const beam::WalletAddress& walletAddr)
{
    emit generatedNewAddress(walletAddr);
}

void WalletModel::onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID)
{
    emit changeCurrentWalletIDs(senderID, receiverID);
}

void WalletModel::onNodeConnectionChanged(bool isNodeConnected)
{
    emit nodeConnectionChanged(isNodeConnected);
}

void WalletModel::onWalletError(beam::wallet::ErrorType error)
{
    emit walletError(error);
}

void WalletModel::FailedToStartWallet()
{
    AppModel::getInstance()->getMessages().addMessage(tr("Failed to start wallet. Please check your wallet data location"));
}

void WalletModel::onSendMoneyVerified()
{
    emit sendMoneyVerified();
}

void WalletModel::onCantSendToExpired()
{
    emit cantSendToExpired();
}

void WalletModel::onPaymentProofExported(const beam::TxID& txID, const beam::ByteBuffer& proof)
{
    string str;
    str.resize(proof.size() * 2);

    beam::to_hex(str.data(), proof.data(), proof.size());
    emit paymentProofExported(txID, QString::fromStdString(str));
}