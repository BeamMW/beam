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

using namespace beam;
using namespace beam::io;
using namespace std;

namespace
{
    static const unsigned LOG_ROTATION_PERIOD = 3 * 60 * 60 * 1000; // 3 hours
}

struct WalletModelBridge : public Bridge<IWalletModelAsync>
{
    BRIDGE_INIT(WalletModelBridge);

    void sendMoney(const beam::WalletID& senderID, const beam::WalletID& receiverID, Amount&& amount, Amount&& fee) override
    {
        tx.send([senderID, receiverID, amount{ move(amount) }, fee{ move(fee) }](BridgeInterface& receiver_) mutable
        {
            receiver_.sendMoney(senderID, receiverID, move(amount), move(fee));
        });
    }

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
        tx.send([amount{move(amount)}](BridgeInterface& receiver_) mutable
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

    void createNewAddress(WalletAddress&& address) override
    {
        tx.send([address{ move(address) }](BridgeInterface& receiver_) mutable
        {
            receiver_.createNewAddress(move(address));
        });
    }

    void changeCurrentWalletIDs(const beam::WalletID& senderID, const beam::WalletID& receiverID) override
    {
        tx.send([senderID, receiverID](BridgeInterface& receiver_) mutable
        {
            receiver_.changeCurrentWalletIDs(senderID, receiverID);
        });
    }

    void generateNewWalletID() override
    {
        tx.send([](BridgeInterface& receiver_) mutable
        {
            receiver_.generateNewWalletID();
        });
    }

    void deleteAddress(const beam::WalletID& id) override
    {
        tx.send([id](BridgeInterface& receiver_) mutable
        {
            receiver_.deleteAddress(id);
        });
    }

    void deleteOwnAddress(const beam::WalletID& id) override
    {
        tx.send([id](BridgeInterface& receiver_) mutable
        {
            receiver_.deleteOwnAddress(id);
        });
    }

    void setNodeAddress(const std::string& addr) override
    {
        tx.send([addr](BridgeInterface& receiver_) mutable
        {
            receiver_.setNodeAddress(addr);
        });
    }

    void emergencyReset() override
    {
        tx.send([](BridgeInterface& receiver_) mutable
        {
            receiver_.emergencyReset();
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
};

WalletModel::WalletModel(IKeyChain::Ptr keychain, IKeyStore::Ptr keystore, const std::string& nodeAddr)
    : _keychain(keychain)
    , _keystore(keystore)
    , _nodeAddrStr(nodeAddr)
{
    qRegisterMetaType<WalletStatus>("WalletStatus");
    qRegisterMetaType<ChangeAction>("beam::ChangeAction");
    qRegisterMetaType<vector<TxDescription>>("std::vector<beam::TxDescription>");
    qRegisterMetaType<vector<TxPeer>>("std::vector<beam::TxPeer>");
    qRegisterMetaType<Amount>("beam::Amount");
    qRegisterMetaType<vector<Coin>>("std::vector<beam::Coin>");
    qRegisterMetaType<vector<WalletAddress>>("std::vector<beam::WalletAddress>");
    qRegisterMetaType<WalletID>("beam::WalletID");
}

WalletModel::~WalletModel()
{
    try
    {
        if (_reactor)
        {
            _reactor->stop();
            wait();
        }
    }
    catch (...)
    {

    }
}

WalletStatus WalletModel::getStatus() const
{
    WalletStatus status{ wallet::getAvailable(_keychain), 0, 0, 0};

    auto history = _keychain->getTxHistory();

    for (const auto& item : history)
    {
        switch (item.m_status)
        {
        case TxDescription::Completed:
            (item.m_sender ? status.sent : status.received) += item.m_amount;
            break;
        default: break;
        }
    }

    status.unconfirmed += wallet::getTotal(_keychain, Coin::Unconfirmed);

    status.update.lastTime = _keychain->getLastUpdateTime();
    ZeroObject(status.stateID);
    _keychain->getSystemStateID(status.stateID);

    return status;
}

void WalletModel::run()
{
    try
    {
        struct WalletSubscriber
        {
            WalletSubscriber(IWalletObserver* client, std::shared_ptr<beam::Wallet> wallet)
                : _client(client)
                , _wallet(wallet)
            {
                _wallet->subscribe(_client);
            }

            ~WalletSubscriber()
            {
                _wallet->unsubscribe(_client);
            }
        private:
            IWalletObserver * _client;
            std::shared_ptr<beam::Wallet> _wallet;
        };

        std::unique_ptr<WalletSubscriber> subscriber;

        _reactor = Reactor::create();
        io::Reactor::GracefulIntHandler gih(*_reactor);

        async = make_shared<WalletModelBridge>(*(static_cast<IWalletModelAsync*>(this)), _reactor);

        emit onStatus(getStatus());
        emit onTxStatus(beam::ChangeAction::Reset, _keychain->getTxHistory());
        emit onTxPeerUpdated(_keychain->getPeers());

        _logRotateTimer = io::Timer::create(_reactor);
        _logRotateTimer->start(
            LOG_ROTATION_PERIOD, true,
            []() {
            Logger::get()->rotate();
        });

        {
            Address node_addr;
            node_addr.resolve(_nodeAddrStr.c_str());
            auto wallet_io = make_shared<WalletNetworkIO>(
                node_addr
                , _keychain
                , _keystore
                , _reactor);
            _wallet_io = wallet_io;
            auto wallet = make_shared<Wallet>(_keychain, wallet_io);
            _wallet = wallet;
            subscriber = make_unique<WalletSubscriber>(static_cast<IWalletObserver*>(this), wallet);
        }

        _reactor->run();
    }
    catch (const runtime_error& ex)
    {
        LOG_ERROR() << ex.what();
        AppModel::getInstance()->getMessages().addMessage(tr("Failed to start wallet. Please check your wallet data location"));
    }
    catch (...)
    {
        LOG_ERROR() << "Unhandled exception";
    }
}

void WalletModel::onStatusChanged()
{
    emit onStatus(getStatus());
}

void WalletModel::onKeychainChanged()
{
    emit onAllUtxoChanged(getUtxos());
    // TODO may be it needs to delete
    onStatusChanged();
}

void WalletModel::onTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items)
{
    emit onTxStatus(action, move(items));
    onStatusChanged();
}

void WalletModel::onSystemStateChanged()
{
    onStatusChanged();
}

void WalletModel::onTxPeerChanged()
{
    emit onTxPeerUpdated(_keychain->getPeers());
}

void WalletModel::onAddressChanged()
{
    emit onAdrresses(true, _keychain->getAddresses(true));
    emit onAdrresses(false, _keychain->getAddresses(false));
}

void WalletModel::onSyncProgress(int done, int total)
{
    emit onSyncProgressUpdated(done, total);
}

void WalletModel::sendMoney(const beam::WalletID& sender, const beam::WalletID& receiver, Amount&& amount, Amount&& fee)
{
    assert(!_wallet.expired());
    auto s = _wallet.lock();
    if (s)
    {
        s->transfer_money(sender, receiver, move(amount), move(fee));
    }
}

void WalletModel::sendMoney(const beam::WalletID& receiver, const std::string& comment, Amount&& amount, Amount&& fee)
{
    try
    {
        WalletID sender;
        _keystore->gen_keypair(sender);

        WalletAddress senderAddress;
        senderAddress.m_walletID = sender;
        senderAddress.m_own = true;
        senderAddress.m_createTime = beam::getTimestamp();

        createNewAddress(std::move(senderAddress));

        ByteBuffer message(comment.begin(), comment.end());

        assert(!_wallet.expired());
        auto s = _wallet.lock();
        if (s)
        {
            s->transfer_money(sender, receiver, move(amount), move(fee), true, move(message));
        }
    }
    catch (...)
    {

    }
}

void WalletModel::syncWithNode()
{
    assert(!_wallet_io.expired());
    auto s = _wallet_io.lock();
    if (s)
    {
        static_pointer_cast<INetworkIO>(s)->connect_node();
    }
}

void WalletModel::calcChange(beam::Amount&& amount)
{
    auto coins = _keychain->selectCoins(amount, false);
    Amount sum = 0;
    for (auto& c : coins)
    {
        sum += c.m_amount;
    }
    if (sum < amount)
    {
        emit onChangeCalculated(0);
    }
    else
    {
        emit onChangeCalculated(sum - amount);
    }
}

void WalletModel::getWalletStatus()
{
    emit onStatus(getStatus());
    emit onTxStatus(beam::ChangeAction::Reset, _keychain->getTxHistory());
    emit onTxPeerUpdated(_keychain->getPeers());
    emit onAdrresses(false, _keychain->getAddresses(false));
}

void WalletModel::getUtxosStatus()
{
    emit onStatus(getStatus());
    emit onAllUtxoChanged(getUtxos());
}

void WalletModel::getAddresses(bool own)
{
    emit onAdrresses(own, _keychain->getAddresses(own));
}

void WalletModel::cancelTx(const beam::TxID& id)
{
    auto w = _wallet.lock();
    if (w)
    {
        w->cancel_tx(id);
    }
}

void WalletModel::deleteTx(const beam::TxID& id)
{
    auto w = _wallet.lock();
    if (w)
    {
        w->delete_tx(id);
    }
}

void WalletModel::createNewAddress(WalletAddress&& address)
{
    _keystore->save_keypair(address.m_walletID, true);
    _keychain->saveAddress(address);

    if (address.m_own)
    {
        auto s = _wallet_io.lock();
        if (s)
        {
            s->new_own_address(address.m_walletID);
        }
    }
}

void WalletModel::changeCurrentWalletIDs(const beam::WalletID& senderID, const beam::WalletID& receiverID)
{
    emit onChangeCurrentWalletIDs(senderID, receiverID);
}

void WalletModel::generateNewWalletID()
{
    try
    {
        WalletID walletID;
        _keystore->gen_keypair(walletID);

        emit onGeneratedNewWalletID(walletID);
    }
    catch (...)
    {

    }
}

void WalletModel::deleteAddress(const beam::WalletID& id)
{
    try
    {
        _keychain->deleteAddress(id);
    }
    catch (...)
    {
    }
}

void WalletModel::deleteOwnAddress(const beam::WalletID& id)
{
    try
    {
        _keystore->erase_key(id);
        _keychain->deleteAddress(id);
        auto s = _wallet_io.lock();
        if (s)
        {
            s->address_deleted(id);
        }
    }
    catch (...)
    {

    }
}

void WalletModel::setNodeAddress(const std::string& addr)
{
    io::Address nodeAddr;

    if (nodeAddr.resolve(addr.c_str()))
    {
        assert(!_wallet.expired());
        auto s = _wallet.lock();
        if (s)
        {
            s->set_node_address(nodeAddr);
        }
    }
    else
    {
        LOG_ERROR() << "Unable to resolve node address: " << addr;
        assert(false);
    }
}

void WalletModel::emergencyReset()
{
    assert(!_wallet.expired());
    auto s = _wallet.lock();
    if (s)
    {
        s->emergencyReset();
    }
}

vector<Coin> WalletModel::getUtxos() const
{
    vector<Coin> utxos;
    _keychain->visit([&utxos](const Coin& c)->bool
    {
        utxos.push_back(c);
        return true;
    });
    return utxos;
}

void WalletModel::changeWalletPassword(const SecString& pass)
{
	_keychain->changePassword(pass);
	_keystore->change_password(pass.data(), pass.size());
}

bool WalletModel::check_receiver_address(const std::string& addr) {
    size_t sz = addr.size();
    if (sz == 0 || sz > 64) return false;
    bool wholeStringIsNumber = false;
    WalletID peerAddr = from_hex(addr, &wholeStringIsNumber);
    if (!wholeStringIsNumber) return false;
    ByteBuffer buff;
    return _keystore->encrypt(buff, "whatever", 8, peerAddr);
}
