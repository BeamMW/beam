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

#include "wallet_client.h"
#include "utility/log_rotation.h"

using namespace beam;
using namespace beam::io;
using namespace std;

namespace
{

template<typename Observer, typename Notifier>
struct ScopedSubscriber
{
    ScopedSubscriber(Observer* observer, const std::shared_ptr<Notifier>& notifier)
        : m_observer(observer)
        , m_notifier(notifier)
    {
        m_notifier->subscribe(m_observer);
    }

    ~ScopedSubscriber()
    {
        m_notifier->unsubscribe(m_observer);
    }
private:
    Observer * m_observer;
    std::shared_ptr<Notifier> m_notifier;
};

using WalletSubscriber = ScopedSubscriber<IWalletObserver, beam::IWallet>;

struct WalletModelBridge : public Bridge<IWalletModelAsync>
{
    BRIDGE_INIT(WalletModelBridge);

    void sendMoney(const beam::WalletID& receiverID, const std::string& comment, beam::Amount&& amount, beam::Amount&& fee) override
    {
        tx.send([receiverID, comment, amount{ move(amount) }, fee{ move(fee) }](BridgeInterface& receiver_) mutable
        {
            receiver_.sendMoney(receiverID, comment, move(amount), move(fee));
        });
    }

    void syncWithNode() override
    {
        tx.send([](BridgeInterface& receiver_) mutable
        {
            receiver_.syncWithNode();
        });
    }

    void calcChange(beam::Amount&& amount) override
    {
        tx.send([amount{ move(amount) }](BridgeInterface& receiver_) mutable
        {
            receiver_.calcChange(move(amount));
        });
    }

    void getWalletStatus() override
    {
        tx.send([](BridgeInterface& receiver_) mutable
        {
            receiver_.getWalletStatus();
        });
    }

    void getUtxosStatus() override
    {
        tx.send([](BridgeInterface& receiver_) mutable
        {
            receiver_.getUtxosStatus();
        });
    }

    void getAddresses(bool own) override
    {
        tx.send([own](BridgeInterface& receiver_) mutable
        {
            receiver_.getAddresses(own);
        });
    }

    void cancelTx(const beam::TxID& id) override
    {
        tx.send([id](BridgeInterface& receiver_) mutable
        {
            receiver_.cancelTx(id);
        });
    }

    void deleteTx(const beam::TxID& id) override
    {
        tx.send([id](BridgeInterface& receiver_) mutable
        {
            receiver_.deleteTx(id);
        });
    }

    void saveAddress(const WalletAddress& address, bool bOwn) override
    {
        tx.send([address, bOwn](BridgeInterface& receiver_) mutable
        {
            receiver_.saveAddress(address, bOwn);
        });
    }

    void changeCurrentWalletIDs(const beam::WalletID& senderID, const beam::WalletID& receiverID) override
    {
        tx.send([senderID, receiverID](BridgeInterface& receiver_) mutable
        {
            receiver_.changeCurrentWalletIDs(senderID, receiverID);
        });
    }

    void generateNewAddress() override
    {
        tx.send([](BridgeInterface& receiver_) mutable
        {
            receiver_.generateNewAddress();
        });
    }

    void deleteAddress(const beam::WalletID& id) override
    {
        tx.send([id](BridgeInterface& receiver_) mutable
        {
            receiver_.deleteAddress(id);
        });
    }

    void saveAddressChanges(const beam::WalletID& id, const std::string& name, bool isNever, bool makeActive, bool makeExpired) override
    {
        tx.send([id, name, isNever, makeActive, makeExpired](BridgeInterface& receiver_) mutable
        {
            receiver_.saveAddressChanges(id, name, isNever, makeActive, makeExpired);
        });
    }

    void setNodeAddress(const std::string& addr) override
    {
        tx.send([addr](BridgeInterface& receiver_) mutable
        {
            receiver_.setNodeAddress(addr);
        });
    }

    void changeWalletPassword(const SecString& pass) override
    {
        // TODO: should be investigated, don't know how to "move" SecString into lambda
        std::string passStr(pass.data(), pass.size());

        tx.send([passStr](BridgeInterface& receiver_) mutable
        {
            receiver_.changeWalletPassword(passStr);
        });
    }

    void getNetworkStatus() override
    {
        tx.send([](BridgeInterface& receiver_) mutable
        {
            receiver_.getNetworkStatus();
        });
    }

    void refresh() override
    {
        tx.send([](BridgeInterface& receiver_) mutable
        {
            receiver_.refresh();
        });
    }

    void exportPaymentProof(const beam::TxID& id) override
    {
        tx.send([id](BridgeInterface& receiver_) mutable
        {
            receiver_.exportPaymentProof(id);
        });
    }
};
}

WalletClient::WalletClient(IWalletDB::Ptr walletDB, const std::string& nodeAddr)
    : m_walletDB(walletDB)
    , m_reactor{ io::Reactor::create() }
    , m_async{ make_shared<WalletModelBridge>(*(static_cast<IWalletModelAsync*>(this)), *m_reactor) }
    , m_isConnected(false)
    , m_nodeAddrStr(nodeAddr)
    , m_isRunning(false)
{
    
}

WalletClient::~WalletClient()
{
    try
    {
        if (m_reactor)
        {
            m_reactor->stop();
            if (m_thread)
            {
                // TODO: check this
                m_thread->join();
            }
        }
    }
    catch (const std::exception& e)
    {
        LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
    }
    catch (...) {
        LOG_UNHANDLED_EXCEPTION();
    }
}

void WalletClient::start()
{
    m_thread = std::make_shared<std::thread>([this]()
    {
        m_isRunning = true;
        try
        {
            std::unique_ptr<WalletSubscriber> wallet_subscriber;

            io::Reactor::Scope scope(*m_reactor);
            io::Reactor::GracefulIntHandler gih(*m_reactor);

            onStatus(getStatus());
            onTxStatus(beam::ChangeAction::Reset, m_walletDB->getTxHistory());

            static const unsigned LOG_ROTATION_PERIOD_SEC = 3*3600; // 3 hours
            static const unsigned LOG_CLEANUP_PERIOD_SEC = 120*3600; // 5 days
            LogRotation logRotation(*m_reactor, LOG_ROTATION_PERIOD_SEC, LOG_CLEANUP_PERIOD_SEC);

            auto wallet = make_shared<Wallet>(m_walletDB);
            m_wallet = wallet;

            struct MyNodeNetwork :public proto::FlyClient::NetworkStd {

                MyNodeNetwork(proto::FlyClient& fc, WalletClient& client)
                    : proto::FlyClient::NetworkStd(fc)
                    , m_walletClient(client)
                {
                }

                WalletClient& m_walletClient;

                void OnNodeConnected(size_t, bool bConnected) override
                {
                    m_walletClient.nodeConnectedStatusChanged(bConnected);
                }

                void OnConnectionFailed(size_t, const proto::NodeConnection::DisconnectReason& reason) override
                {
                    m_walletClient.nodeConnectionFailed(reason);
                }
            };

            auto nodeNetwork = make_shared<MyNodeNetwork>(*wallet, *this);

            Address nodeAddr;
            if (nodeAddr.resolve(m_nodeAddrStr.c_str()))
            {
                nodeNetwork->m_Cfg.m_vNodes.push_back(nodeAddr);
            }
            else
            {
                LOG_ERROR() << "Unable to resolve node address: " << m_nodeAddrStr;
            }

            nodeNetwork->m_Cfg.m_vNodes.push_back(nodeAddr);

            m_nodeNetwork = nodeNetwork;

            auto walletNetwork = make_shared<WalletNetworkViaBbs>(*wallet, *nodeNetwork, m_walletDB);
            m_walletNetwork = walletNetwork;
            wallet->set_Network(*nodeNetwork, *walletNetwork);

            wallet_subscriber = make_unique<WalletSubscriber>(static_cast<IWalletObserver*>(this), wallet);

            nodeNetwork->Connect();

            m_reactor->run();
        }
        catch (const runtime_error& ex)
        {
            LOG_ERROR() << ex.what();
            FailedToStartWallet();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
        m_isRunning = false;
    });
}

IWalletModelAsync::Ptr WalletClient::getAsync()
{
    return m_async;
}

std::string WalletClient::getNodeAddress() const
{
    return m_nodeAddrStr;
}

bool WalletClient::isRunning() const
{
    return m_isRunning;
}

void WalletClient::onCoinsChanged()
{
    onAllUtxoChanged(getUtxos());
    // TODO may be it needs to delete
    onStatus(getStatus());
}

void WalletClient::onTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items)
{
    onTxStatus(action, move(items));
    onStatus(getStatus());
}

void WalletClient::onSystemStateChanged()
{
    onStatus(getStatus());
}

void WalletClient::onAddressChanged()
{
    onAddresses(true, m_walletDB->getAddresses(true));
    onAddresses(false, m_walletDB->getAddresses(false));
}

void WalletClient::onSyncProgress(int done, int total)
{
    onSyncProgressUpdated(done, total);
}

void WalletClient::sendMoney(const beam::WalletID& receiver, const std::string& comment, Amount&& amount, Amount&& fee)
{
    try
    {
        auto receiverAddr = m_walletDB->getAddress(receiver);

        if (!receiverAddr)
        {
            WalletAddress peerAddr;
            peerAddr.m_walletID = receiver;
            peerAddr.m_createTime = getTimestamp();
            peerAddr.m_label = comment;

            saveAddress(peerAddr, false);
        }

        WalletAddress senderAddress = wallet::createAddress(*m_walletDB);
        senderAddress.m_label = comment;
        saveAddress(senderAddress, true); // should update the wallet_network

        ByteBuffer message(comment.begin(), comment.end());

        assert(!m_wallet.expired());
        auto s = m_wallet.lock();
        if (s)
        {
            s->transfer_money(senderAddress.m_walletID, receiver, move(amount), move(fee), true, 120, 720, move(message));
        }

        onSendMoneyVerified();
    }
    catch (const beam::AddressExpiredException&)
    {
        onCantSendToExpired();
        return;
    }
    catch (const std::exception& e)
    {
        LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
    }
    catch (...) {
        LOG_UNHANDLED_EXCEPTION();
    }
}

void WalletClient::syncWithNode()
{
    assert(!m_nodeNetwork.expired());
    auto s = m_nodeNetwork.lock();
    if (s)
        s->Connect();
}

void WalletClient::calcChange(beam::Amount&& amount)
{
    auto coins = m_walletDB->selectCoins(amount);
    Amount sum = 0;
    for (auto& c : coins)
    {
        sum += c.m_ID.m_Value;
    }
    if (sum < amount)
    {
        onChangeCalculated(0);
    }
    else
    {
        onChangeCalculated(sum - amount);
    }
}

void WalletClient::getWalletStatus()
{
    onStatus(getStatus());
    onTxStatus(beam::ChangeAction::Reset, m_walletDB->getTxHistory());
    onAddresses(false, m_walletDB->getAddresses(false));
}

void WalletClient::getUtxosStatus()
{
    onStatus(getStatus());
    onAllUtxoChanged(getUtxos());
}

void WalletClient::getAddresses(bool own)
{
    onAddresses(own, m_walletDB->getAddresses(own));
}

void WalletClient::cancelTx(const beam::TxID& id)
{
    auto w = m_wallet.lock();
    if (w)
    {
        static_pointer_cast<IWallet>(w)->cancel_tx(id);
    }
}

void WalletClient::deleteTx(const beam::TxID& id)
{
    auto w = m_wallet.lock();
    if (w)
    {
        static_pointer_cast<IWallet>(w)->delete_tx(id);
    }
}

void WalletClient::saveAddress(const WalletAddress& address, bool bOwn)
{
    m_walletDB->saveAddress(address);

    if (bOwn)
    {
        auto s = m_walletNetwork.lock();
        if (s)
        {
            static_pointer_cast<WalletNetworkViaBbs>(s)->AddOwnAddress(address);
        }
    }
}

void WalletClient::changeCurrentWalletIDs(const beam::WalletID& senderID, const beam::WalletID& receiverID)
{
    onChangeCurrentWalletIDs(senderID, receiverID);
}

void WalletClient::generateNewAddress()
{
    try
    {
        WalletAddress address = wallet::createAddress(*m_walletDB);

        onGeneratedNewAddress(address);
    }
    catch (const std::exception& e)
    {
        LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
    }
    catch (...) {
        LOG_UNHANDLED_EXCEPTION();
    }
}

void WalletClient::deleteAddress(const beam::WalletID& id)
{
    try
    {
        auto pVal = m_walletDB->getAddress(id);
        if (pVal)
        {
            if (pVal->m_OwnID)
            {
                auto s = m_walletNetwork.lock();
                if (s)
                {
                    static_pointer_cast<WalletNetworkViaBbs>(s)->DeleteOwnAddress(pVal->m_OwnID);
                }
            }
            m_walletDB->deleteAddress(id);
        }
    }
    catch (const std::exception& e)
    {
        LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
    }
    catch (...) {
        LOG_UNHANDLED_EXCEPTION();
    }
}

void WalletClient::saveAddressChanges(const beam::WalletID& id, const std::string& name, bool makeEternal, bool makeActive, bool makeExpired)
{
    try
    {
        auto addr = m_walletDB->getAddress(id);

        if (addr)
        {
            if (addr->m_OwnID)
            {
                addr->setLabel(name);
                if (makeExpired)
                {
                    addr->makeExpired();
                }
                else if (makeEternal)
                {
                    addr->makeEternal();
                }
                else if (makeActive)
                {
                    // set expiration date to 24h since now
                    addr->makeActive(24 * 60 * 60);
                }

                m_walletDB->saveAddress(*addr);

                auto s = m_walletNetwork.lock();
                if (s)
                {
                    static_pointer_cast<WalletNetworkViaBbs>(s)->AddOwnAddress(*addr);
                }
            }
            else
            {
                LOG_ERROR() << "It's not implemented!";
            }
        }
        else
        {
            LOG_ERROR() << "Address " << to_string(id) << " is absent.";
        }
    }
    catch (const std::exception& e)
    {
        LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
    }
    catch (...) {
        LOG_UNHANDLED_EXCEPTION();
    }
}

void WalletClient::setNodeAddress(const std::string& addr)
{
    io::Address nodeAddr;

    if (nodeAddr.resolve(addr.c_str()))
    {
        assert(!m_nodeNetwork.expired());
        auto s = m_nodeNetwork.lock();
        if (s)
        {
            s->Disconnect();

            static_cast<proto::FlyClient::NetworkStd&>(*s).m_Cfg.m_vNodes.clear();
            static_cast<proto::FlyClient::NetworkStd&>(*s).m_Cfg.m_vNodes.push_back(nodeAddr);

            s->Connect();
        }
    }
    else
    {
        LOG_ERROR() << "Unable to resolve node address: " << addr;
    }
}

void WalletClient::changeWalletPassword(const SecString& pass)
{
    m_walletDB->changePassword(pass);
}

void WalletClient::getNetworkStatus()
{
    if (m_walletError.is_initialized() && !m_isConnected)
    {
        onWalletError(*m_walletError);
        return;
    }

    onNodeConnectionChanged(m_isConnected);
}

void WalletClient::refresh()
{
    try
    {
        assert(!m_wallet.expired());
        auto s = m_wallet.lock();
        if (s)
        {
            s->Refresh();
        }
    }
    catch (const std::exception& e)
    {
        LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
    }
    catch (...) {
        LOG_UNHANDLED_EXCEPTION();
    }
}

void WalletClient::exportPaymentProof(const beam::TxID& id)
{
    onPaymentProofExported(id, beam::wallet::ExportPaymentProof(*m_walletDB, id));
}

WalletStatus WalletClient::getStatus() const
{
    WalletStatus status;
	wallet::Totals totals(*m_walletDB);

    status.available = totals.Avail;
    status.receiving = totals.Incoming;
    status.sending = totals.Outgoing;
    status.maturing = totals.Maturing;

    status.update.lastTime = m_walletDB->getLastUpdateTime();

    ZeroObject(status.stateID);
    m_walletDB->getSystemStateID(status.stateID);

    return status;
}

vector<Coin> WalletClient::getUtxos() const
{
    vector<Coin> utxos;
    m_walletDB->visit([&utxos](const Coin& c)->bool
    {
        utxos.push_back(c);
        return true;
    });
    return utxos;
}

void WalletClient::nodeConnectionFailed(const proto::NodeConnection::DisconnectReason& reason)
{
    m_isConnected = false;

    // reason -> wallet::ErrorType
    if (proto::NodeConnection::DisconnectReason::ProcessingExc == reason.m_Type)
    {
        m_walletError = wallet::getWalletError(reason.m_ExceptionDetails.m_ExceptionType);
        onWalletError(*m_walletError);
        return;
    }

    if (proto::NodeConnection::DisconnectReason::Io == reason.m_Type)
    {
        m_walletError = wallet::getWalletError(reason.m_IoError);
        onWalletError(*m_walletError);
        return;
    }

    LOG_ERROR() << "Unprocessed error: " << reason;
}

void WalletClient::nodeConnectedStatusChanged(bool isNodeConnected)
{
    m_isConnected = isNodeConnected;
    onNodeConnectionChanged(isNodeConnected);
}