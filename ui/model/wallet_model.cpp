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
using namespace beam::wallet;
using namespace beam::io;
using namespace std;

WalletModel::WalletModel(IWalletDB::Ptr walletDB, IPrivateKeyKeeper::Ptr keyKeeper, const std::string& nodeAddr, beam::io::Reactor::Ptr reactor)
    : WalletClient(walletDB, nodeAddr, reactor, keyKeeper)
{
    qRegisterMetaType<beam::wallet::WalletStatus>("beam::wallet::WalletStatus");
    qRegisterMetaType<beam::wallet::ChangeAction>("beam::wallet::ChangeAction");
    qRegisterMetaType<vector<beam::wallet::TxDescription>>("std::vector<beam::wallet::TxDescription>");
    qRegisterMetaType<vector<beam::wallet::SwapOffer>>("std::vector<beam::wallet::SwapOffer>");
    qRegisterMetaType<beam::Amount>("beam::Amount");
    qRegisterMetaType<vector<beam::wallet::Coin>>("std::vector<beam::wallet::Coin>");
    qRegisterMetaType<vector<beam::wallet::WalletAddress>>("std::vector<beam::wallet::WalletAddress>");
    qRegisterMetaType<beam::wallet::WalletID>("beam::wallet::WalletID");
    qRegisterMetaType<beam::wallet::WalletAddress>("beam::wallet::WalletAddress");
    qRegisterMetaType<beam::wallet::ErrorType>("beam::wallet::ErrorType");
    qRegisterMetaType<beam::wallet::TxID>("beam::wallet::TxID");
    qRegisterMetaType<beam::wallet::TxParameters>("beam::wallet::TxParameters");

    connect(this, SIGNAL(walletStatus(const beam::wallet::WalletStatus&)), this, SLOT(setStatus(const beam::wallet::WalletStatus&)));
    connect(this, SIGNAL(addressesChanged(bool, const std::vector<beam::wallet::WalletAddress>&)),
            this, SLOT(setAddresses(bool, const std::vector<beam::wallet::WalletAddress>&)));
}

WalletModel::~WalletModel()
{
    stopReactor();
}

QString WalletModel::GetErrorString(beam::wallet::ErrorType type)
{
    // TODO: add more detailed error description
    switch (type)
    {
    case wallet::ErrorType::NodeProtocolBase:
        //% "Node protocol error!"
        return qtTrId("wallet-model-node-protocol-error");
    case wallet::ErrorType::NodeProtocolIncompatible:
        //% "You are trying to connect to incompatible peer."
        return qtTrId("wallet-model-incompatible-peer-error");
    case wallet::ErrorType::ConnectionBase:
        //% "Connection error"
        return qtTrId("wallet-model-connection-base-error");
    case wallet::ErrorType::ConnectionTimedOut:
        //% "Connection timed out"
        return qtTrId("wallet-model-connection-time-out-error");
    case wallet::ErrorType::ConnectionRefused:
        //% "Cannot connect to node"
        return qtTrId("wallet-model-connection-refused-error") + ": " +  getNodeAddress().c_str();
    case wallet::ErrorType::ConnectionHostUnreach:
        //% "Node is unreachable"
        return qtTrId("wallet-model-connection-host-unreach-error") + ": " + getNodeAddress().c_str();
    case wallet::ErrorType::ConnectionAddrInUse:
    {
        auto localNodePort = AppModel::getInstance().getSettings().getLocalNodePort();
        //% "The port %1 is already in use. Check if a wallet is already running on this machine or change the port settings."
        return qtTrId("wallet-model-connection-addr-in-use-error").arg(QString::number(localNodePort));
    }
    case wallet::ErrorType::TimeOutOfSync:
        //% "System time not synchronized"
        return qtTrId("wallet-model-time-sync-error");
    case wallet::ErrorType::HostResolvedError:
        //% "Incorrect node name or no Internet connection."
        return qtTrId("wallet-model-host-unresolved-error");
    default:
        //% "Unexpected error!"
        return qtTrId("wallet-model-undefined-error");
    }
}

bool WalletModel::isOwnAddress(const WalletID& walletID) const
{
    for (const auto& it: m_addresses)
    {
        if (it.m_walletID == walletID) {
            return true;
        }
    }
    return false;
}

bool WalletModel::isAddressWithCommentExist(const std::string& comment) const
{
    if (comment.empty())
    {
        return false;
    }
    for (const auto& it: m_addresses)
    {
        if (it.m_label == comment) {
            return true;
        }
    }
    return false;
}

void WalletModel::onStatus(const beam::wallet::WalletStatus& status)
{
    emit walletStatus(status);
}

void WalletModel::onTxStatus(beam::wallet::ChangeAction action, const std::vector<beam::wallet::TxDescription>& items)
{
    emit transactionsChanged(action, items);
}

void WalletModel::onSyncProgressUpdated(int done, int total)
{
    emit syncProgressUpdated(done, total);
}

void WalletModel::onChangeCalculated(beam::Amount change)
{
    emit changeCalculated(change);
}

void WalletModel::onAllUtxoChanged(const std::vector<beam::wallet::Coin>& utxos)
{
    emit allUtxoChanged(utxos);
}

void WalletModel::onAddresses(bool own, const std::vector<beam::wallet::WalletAddress>& addrs)
{
    emit addressesChanged(own, addrs);
}

void WalletModel::onSwapOffersChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::SwapOffer>& offers)
{
    emit swapOffersChanged(action, offers);
}

void WalletModel::onCoinsByTx(const std::vector<beam::wallet::Coin>& coins)
{
}

void WalletModel::onAddressChecked(const std::string& addr, bool isValid)
{
    emit addressChecked(QString::fromStdString(addr), isValid);
}

void WalletModel::onImportRecoveryProgress(uint64_t done, uint64_t total)
{
}

void WalletModel::onShowKeyKeeperMessage()
{
#if defined(BEAM_HW_WALLET)
    emit showTrezorMessage();
#endif
}

void WalletModel::onHideKeyKeeperMessage()
{
#if defined(BEAM_HW_WALLET)
    emit hideTrezorMessage();
#endif
}

void WalletModel::onShowKeyKeeperError(const std::string& error)
{
#if defined(BEAM_HW_WALLET)
    emit showTrezorError(QString::fromStdString(error));
#endif
}

void WalletModel::onGeneratedNewAddress(const beam::wallet::WalletAddress& walletAddr)
{
    emit generatedNewAddress(walletAddr);
}

void WalletModel::onNewAddressFailed()
{
    emit newAddressFailed();
}

void WalletModel::onNoDeviceConnected()
{
#if defined(BEAM_HW_WALLET)
    //% "There is no Trezor device connected. Please, connect and try again."
    showTrezorError(qtTrId("wallet-model-device-not-connected"));
#endif
}

void WalletModel::onChangeCurrentWalletIDs(beam::wallet::WalletID senderID, beam::wallet::WalletID receiverID)
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
    //% "Failed to start wallet. Please check your wallet data location"
    AppModel::getInstance().getMessages().addMessage(qtTrId("wallet-model-data-location-error"));
}

void WalletModel::onSendMoneyVerified()
{
    emit sendMoneyVerified();
}

void WalletModel::onCantSendToExpired()
{
    emit cantSendToExpired();
}

void WalletModel::onPaymentProofExported(const beam::wallet::TxID& txID, const beam::ByteBuffer& proof)
{
    string str;
    str.resize(proof.size() * 2);

    beam::to_hex(str.data(), proof.data(), proof.size());
    emit paymentProofExported(txID, QString::fromStdString(str));
}

beam::Amount WalletModel::getAvailable() const
{
    return m_status.available;
}

beam::Amount WalletModel::getReceiving() const
{
    return m_status.receiving;
}

beam::Amount WalletModel::getReceivingIncoming() const
{
    return m_status.receivingIncoming;
}

beam::Amount WalletModel::getReceivingChange() const
{
    return m_status.receivingChange;
}

beam::Amount WalletModel::getSending() const
{
    return m_status.sending;
}

beam::Amount WalletModel::getMaturing() const
{
    return m_status.maturing;
}

beam::Height WalletModel::getCurrentHeight() const
{
    return m_status.stateID.m_Height;
}

beam::Block::SystemState::ID WalletModel::getCurrentStateID() const
{
    return m_status.stateID;
}

void WalletModel::setStatus(const beam::wallet::WalletStatus& status)
{
    if (m_status.available != status.available)
    {
        m_status.available = status.available;
        emit availableChanged();
    }

    if (m_status.receiving != status.receiving)
    {
        m_status.receiving = status.receiving;
        emit receivingChanged();
    }

    if (m_status.receivingIncoming != status.receivingIncoming)
    {
        m_status.receivingIncoming = status.receivingIncoming;
        emit receivingIncomingChanged();
    }

    if (m_status.receivingChange != status.receivingChange)
    {
        m_status.receivingChange = status.receivingChange;
        emit receivingChangeChanged();
    }

    if (m_status.sending != status.sending)
    {
        m_status.sending = status.sending;
        emit sendingChanged();
    }

    if (m_status.maturing != status.maturing)
    {
        m_status.maturing = status.maturing;
        emit maturingChanged();
    }

    if (m_status.stateID != status.stateID)
    {
        m_status.stateID = status.stateID;
        emit stateIDChanged();
    }
}

void WalletModel::setAddresses(bool own, const std::vector<beam::wallet::WalletAddress>& addrs)
{
    if (own)
    {
        m_addresses = addrs;
    }
}
