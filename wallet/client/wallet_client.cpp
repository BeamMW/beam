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
#include "wallet/core/simple_transaction.h"
#include "utility/log_rotation.h"
#include "core/block_rw.h"
#include "keykeeper/trezor_key_keeper.h"
#include "wallet/client/extensions/newscast/newscast.h"

using namespace std;

namespace
{
using namespace beam;
using namespace beam::wallet;

const size_t kCollectorBufferSize = 50;

using WalletSubscriber = ScopedSubscriber<wallet::IWalletObserver, wallet::Wallet>;

struct WalletModelBridge : public Bridge<IWalletModelAsync>
{
    BRIDGE_INIT(WalletModelBridge);

    void sendMoney(const wallet::WalletID& receiverID, const std::string& comment, Amount&& amount, Amount&& fee) override
    {
        typedef void(IWalletModelAsync::*SendMoneyType)(const wallet::WalletID&, const std::string&, Amount&&, Amount&&);
        call_async((SendMoneyType)&IWalletModelAsync::sendMoney, receiverID, comment, move(amount), move(fee));
    }

    void sendMoney(const wallet::WalletID& senderID, const wallet::WalletID& receiverID, const std::string& comment, Amount&& amount, Amount&& fee) override
    {
        typedef void(IWalletModelAsync::*SendMoneyType)(const wallet::WalletID &, const wallet::WalletID &, const std::string &, Amount &&, Amount &&);
        call_async((SendMoneyType)&IWalletModelAsync::sendMoney, senderID, receiverID, comment, move(amount), move(fee));
    }

    void startTransaction(TxParameters&& parameters) override
    {
        call_async(&IWalletModelAsync::startTransaction, move(parameters));
    }

    void syncWithNode() override
    {
        call_async(&IWalletModelAsync::syncWithNode);
    }

    void calcChange(Amount&& amount) override
    {
        call_async(&IWalletModelAsync::calcChange, move(amount));
    }

    void getWalletStatus() override
    {
        call_async(&IWalletModelAsync::getWalletStatus);
    }

    void getTransactions() override
    {
        call_async(&IWalletModelAsync::getTransactions);
    }

    void getUtxosStatus() override
    {
        call_async(&IWalletModelAsync::getUtxosStatus);
    }

    void getAddresses(bool own) override
    {
        call_async(&IWalletModelAsync::getAddresses, own);
    }
    
#ifdef BEAM_ATOMIC_SWAP_SUPPORT    
    void getSwapOffers() override
    {
		call_async(&IWalletModelAsync::getSwapOffers);
    }

    void publishSwapOffer(const wallet::SwapOffer& offer) override
    {
		call_async(&IWalletModelAsync::publishSwapOffer, offer);
    }

    void loadSwapParams() override
    {
        call_async(&IWalletModelAsync::loadSwapParams);
    }

    void storeSwapParams(const beam::ByteBuffer& params) override
    {
        call_async(&IWalletModelAsync::storeSwapParams, params);
    }
#endif
    void cancelTx(const wallet::TxID& id) override
    {
        call_async(&IWalletModelAsync::cancelTx, id);
    }

    void deleteTx(const wallet::TxID& id) override
    {
        call_async(&IWalletModelAsync::deleteTx, id);
    }

    void getCoinsByTx(const wallet::TxID& id) override
    {
        call_async(&IWalletModelAsync::getCoinsByTx, id);
    }

    void saveAddress(const wallet::WalletAddress& address, bool bOwn) override
    {
        call_async(&IWalletModelAsync::saveAddress, address, bOwn);
    }

    void changeCurrentWalletIDs(const wallet::WalletID& senderID, const wallet::WalletID& receiverID) override
    {
        call_async(&IWalletModelAsync::changeCurrentWalletIDs, senderID, receiverID);
    }


    void generateNewAddress() override
    {
        call_async(&IWalletModelAsync::generateNewAddress);
    }

    void deleteAddress(const wallet::WalletID& id) override
    {
        call_async(&IWalletModelAsync::deleteAddress, id);
    }

    void updateAddress(const wallet::WalletID& id, const std::string& name, WalletAddress::ExpirationStatus status) override
    {
        call_async(&IWalletModelAsync::updateAddress, id, name, status);
    }

    void setNodeAddress(const std::string& addr) override
    {
        call_async(&IWalletModelAsync::setNodeAddress, addr);
    }

    void changeWalletPassword(const SecString& pass) override
    {
        // TODO: should be investigated, don't know how to "move" SecString into lambda
        std::string passStr(pass.data(), pass.size());

        call_async(&IWalletModelAsync::changeWalletPassword, passStr);
    }

    void getNetworkStatus() override
    {
        call_async(&IWalletModelAsync::getNetworkStatus);
    }

    void rescan() override
    {
        call_async(&IWalletModelAsync::rescan);
    }

    void exportPaymentProof(const wallet::TxID& id) override
    {
        call_async(&IWalletModelAsync::exportPaymentProof, id);
    }

    void checkAddress(const std::string& addr) override
    {
        call_async(&IWalletModelAsync::checkAddress, addr);
    }

    void importRecovery(const std::string& path) override
    {
        call_async(&IWalletModelAsync::importRecovery, path);
    }

    void importDataFromJson(const std::string& data) override
    {
        call_async(&IWalletModelAsync::importDataFromJson, data);
    }

    void exportDataToJson() override
    {
        call_async(&IWalletModelAsync::exportDataToJson);
    }

    void exportTxHistoryToCsv() override
    {
        call_async(&IWalletModelAsync::exportTxHistoryToCsv);
    }
};
}

namespace beam::wallet
{
    WalletClient::WalletClient(IWalletDB::Ptr walletDB, const std::string& nodeAddr, io::Reactor::Ptr reactor, IPrivateKeyKeeper::Ptr keyKeeper)
        : m_walletDB(walletDB)
        , m_reactor{ reactor ? reactor : io::Reactor::create() }
        , m_async{ make_shared<WalletModelBridge>(*(static_cast<IWalletModelAsync*>(this)), *m_reactor) }
        , m_connectedNodesCount(0)
        , m_trustedConnectionCount(0)
        , m_initialNodeAddrStr(nodeAddr)
        , m_keyKeeper(keyKeeper)
        , m_CoinChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) { onAllUtxoChanged(action, items); })
        , m_AddressChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) { onAddressesChanged(action, items); })
        , m_TransactionChangesCollector(kCollectorBufferSize, m_reactor, [this](auto action, const auto& items) { onTxStatus(action, items); })
    {
        m_keyKeeper->subscribe(this);
    }

    WalletClient::~WalletClient()
    {
        // reactor should be already stopped here, but just in case
        // this call is unsafe and may result in crash if reactor is not stopped
        assert(!m_thread && !m_reactor);
        stopReactor();
    }

    void WalletClient::stopReactor()
    {
        try
        {
            if (m_reactor)
            {
                if (m_thread)
                {
                    m_reactor->stop();
                    m_thread->join();
                    m_thread.reset();
                }
                m_reactor.reset();
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

    void WalletClient::postFunctionToClientContext(MessageFunction&& func)
    {
        onPostFunctionToClientContext(move(func));
    }

    void WalletClient::start(std::shared_ptr<std::unordered_map<TxType, BaseTransaction::Creator::Ptr>> txCreators, std::string newsPublisherKey)
    {
        m_thread = std::make_shared<std::thread>([this, txCreators, newsPublisherKey]()
            {
                try
                {
					io::Reactor::Scope scope(*m_reactor);
					io::Reactor::GracefulIntHandler gih(*m_reactor);

                    static const unsigned LOG_ROTATION_PERIOD_SEC = 3 * 3600; // 3 hours
                    static const unsigned LOG_CLEANUP_PERIOD_SEC = 120 * 3600; // 5 days
                    LogRotation logRotation(*m_reactor, LOG_ROTATION_PERIOD_SEC, LOG_CLEANUP_PERIOD_SEC);

                    auto wallet = make_shared<Wallet>(m_walletDB, m_keyKeeper);
                    m_wallet = wallet;

                    if (txCreators)
                    {
                        for (auto&[txType, creator] : *txCreators)
                        {
                            wallet->RegisterTransactionType(txType, creator);
                        }
                    }

                    wallet->ResumeAllTransactions();

                    updateClientState();

                    auto nodeNetwork = make_shared<NodeNetwork>(*wallet, m_initialNodeAddrStr);
                    m_nodeNetwork = nodeNetwork;

                    using NodeNetworkSubscriber = ScopedSubscriber<INodeConnectionObserver, NodeNetwork>;
                    auto nodeNetworkSubscriber = std::make_unique<NodeNetworkSubscriber>(static_cast<INodeConnectionObserver*>(this), nodeNetwork);

                    auto walletNetwork = make_shared<WalletNetworkViaBbs>(*wallet, nodeNetwork, m_walletDB, m_keyKeeper);
                    m_walletNetwork = walletNetwork;
                    wallet->SetNodeEndpoint(nodeNetwork);
                    wallet->AddMessageEndpoint(walletNetwork);

                    auto wallet_subscriber = make_unique<WalletSubscriber>(static_cast<IWalletObserver*>(this), wallet);

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
                    OfferBoardProtocolHandler protocolHandler(m_keyKeeper->get_SbbsKdf(), m_walletDB);

                    auto offersBulletinBoard = make_shared<SwapOffersBoard>(*nodeNetwork, *walletNetwork, protocolHandler);
                    m_offersBulletinBoard = offersBulletinBoard;

                    using WalletDbSubscriber = ScopedSubscriber<IWalletDbObserver, IWalletDB>;
                    using SwapOffersBoardSubscriber = ScopedSubscriber<ISwapOffersObserver, SwapOffersBoard>;

                    auto walletDbSubscriber = make_unique<WalletDbSubscriber>(static_cast<IWalletDbObserver*>(offersBulletinBoard.get()), m_walletDB);
                    auto swapOffersBoardSubscriber = make_unique<SwapOffersBoardSubscriber>(static_cast<ISwapOffersObserver*>(this), offersBulletinBoard);
#endif
                    auto newscastParser = make_shared<NewscastProtocolParser>();
                    newscastParser->setPublisherKeys( { NewscastProtocolParser::stringToPublicKey(newsPublisherKey) } );
                    m_newscastParser = newscastParser;
                    auto newscast = make_shared<Newscast>(*nodeNetwork, *newscastParser);
                    m_newscast = newscast;
                    using NewsSubscriber = ScopedSubscriber<INewsObserver, Newscast>;
                    auto newsSubscriber = make_unique<NewsSubscriber>(static_cast<INewsObserver*>(this), newscast);

                    nodeNetwork->tryToConnect();
                    m_reactor->run_ex([&wallet, &nodeNetwork](){
                        wallet->CleanupNetwork();
                        nodeNetwork->Disconnect();
                    });

                    assert(walletNetwork.use_count() == 1);
                    walletNetwork.reset();

                    nodeNetworkSubscriber.reset();
                    assert(nodeNetwork.use_count() == 1);
                    nodeNetwork.reset();
                }
                catch (const runtime_error& ex)
                {
                    LOG_ERROR() << ex.what();
                    FailedToStartWallet();
                }
                catch (...) {
                    LOG_UNHANDLED_EXCEPTION();
                }
            });
    }

    IWalletModelAsync::Ptr WalletClient::getAsync()
    {
        return m_async;
    }

    std::string WalletClient::getNodeAddress() const
    {
        if (auto s = m_nodeNetwork.lock())
        {
            return s->getNodeAddress();
        }
        else
        {
            return m_initialNodeAddrStr;
        }
    }

    std::string WalletClient::exportOwnerKey(const beam::SecString& pass) const
    {
        Key::IKdf::Ptr pKey = m_walletDB->get_MasterKdf();
        const ECC::HKdf& kdf = static_cast<ECC::HKdf&>(*pKey);

        KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
        ks.m_sMeta = std::to_string(0);

        ECC::HKdfPub pkdf;
        pkdf.GenerateFrom(kdf);

        ks.Export(pkdf);

        return ks.m_sRes;
    }

    bool WalletClient::isRunning() const
    {
        return m_thread && m_thread->joinable();
    }

    bool WalletClient::isFork1() const
    {
        return m_currentHeight >= Rules::get().pForks[1].m_Height;
    }

    size_t WalletClient::getUnsafeActiveTransactionsCount() const
    {
        return m_unsafeActiveTxCount;
    }

    bool WalletClient::isConnectionTrusted() const
    {
        return m_isConnectionTrusted;
    }

    void WalletClient::onCoinsChanged(ChangeAction action, const std::vector<Coin>& items)
    {
        m_CoinChangesCollector.CollectItems(action, items);
        // TODO: refactor this
        // We should call getStatus to update balances
        onStatus(getStatus());
    }

    void WalletClient::onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items)
    {
        m_TransactionChangesCollector.CollectItems(action, items);
        updateClientTxState();
    }

    void WalletClient::onSystemStateChanged(const Block::SystemState::ID& stateID)
    {
        onStatus(getStatus());
        updateClientState();
    }

    void WalletClient::onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items)
    {
        m_AddressChangesCollector.CollectItems(action, items);
    }

    void WalletClient::onSyncProgress(int done, int total)
    {
        onSyncProgressUpdated(done, total);
    }

    void WalletClient::onOwnedNode(const PeerID& id, bool connected)
    {
        updateConnectionTrust(connected);
        onNodeConnectionChanged(isConnected());
    }

    void WalletClient::sendMoney(const WalletID& receiver, const std::string& comment, Amount&& amount, Amount&& fee)
    {
        try
        {
            assert(!m_wallet.expired());
            auto s = m_wallet.lock();
            if (s)
            {
                WalletAddress senderAddress = storage::createAddress(*m_walletDB, m_keyKeeper);
                saveAddress(senderAddress, true); // should update the wallet_network

                ByteBuffer message(comment.begin(), comment.end());


                s->StartTransaction(CreateSimpleTransactionParameters()
                    .SetParameter(TxParameterID::MyID, senderAddress.m_walletID)
                    .SetParameter(TxParameterID::PeerID, receiver)
                    .SetParameter(TxParameterID::Amount, amount)
                    .SetParameter(TxParameterID::Fee, fee)
                    .SetParameter(TxParameterID::Message, message));
            }

            onSendMoneyVerified();
        }
        catch (const CannotGenerateSecretException&)
        {
            onNewAddressFailed();
            return;
        }
        catch (const AddressExpiredException&)
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

    void WalletClient::sendMoney(const WalletID& sender, const WalletID& receiver, const std::string& comment, Amount&& amount, Amount&& fee)
    {
        try
        {
            assert(!m_wallet.expired());
            auto s = m_wallet.lock();
            if (s)
            {
                ByteBuffer message(comment.begin(), comment.end());
                s->StartTransaction(CreateSimpleTransactionParameters()
                    .SetParameter(TxParameterID::MyID, sender)
                    .SetParameter(TxParameterID::PeerID, receiver)
                    .SetParameter(TxParameterID::Amount, amount)
                    .SetParameter(TxParameterID::Fee, fee)
                    .SetParameter(TxParameterID::Message, message));
            }

            onSendMoneyVerified();
        }
        catch (const CannotGenerateSecretException&)
        {
            onNewAddressFailed();
            return;
        }
        catch (const AddressExpiredException&)
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

    void WalletClient::startTransaction(TxParameters&& parameters)
    {
        try
        {
            assert(!m_wallet.expired());
            auto s = m_wallet.lock();
            if (s)
            {
                auto myID = parameters.GetParameter<WalletID>(TxParameterID::MyID);
                if (!myID)
                {
                    WalletAddress senderAddress = storage::createAddress(*m_walletDB, m_keyKeeper);
                    saveAddress(senderAddress, true); // should update the wallet_network
                
                    parameters.SetParameter(TxParameterID::MyID, senderAddress.m_walletID);
                }

                s->StartTransaction(parameters);
            }

            onSendMoneyVerified();
        }
        catch (const CannotGenerateSecretException&)
        {
            onNewAddressFailed();
            return;
        }
        catch (const AddressExpiredException&)
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
        if (auto s = m_nodeNetwork.lock())
        {
            s->Connect();
        }
    }

    void WalletClient::calcChange(Amount&& amount)
    {
        auto coins = m_walletDB->selectCoins(amount, Zero);
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
    }

    void WalletClient::getTransactions()
    {
        onTxStatus(ChangeAction::Reset, m_walletDB->getTxHistory(wallet::TxType::ALL));
    }

    void WalletClient::getUtxosStatus()
    {
        onAllUtxoChanged(ChangeAction::Reset, getUtxos());
    }

    void WalletClient::getAddresses(bool own)
    {
        onAddresses(own, m_walletDB->getAddresses(own));
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    void WalletClient::getSwapOffers()
    {
        if (auto p = m_offersBulletinBoard.lock())
        {
            onSwapOffersChanged(ChangeAction::Reset, p->getOffersList());
        }
    }

    void WalletClient::publishSwapOffer(const SwapOffer& offer)
    {
        if (auto p = m_offersBulletinBoard.lock())
        {
            p->publishOffer(offer);
        }
    }

    namespace {
        const char* SWAP_PARAMS_NAME = "LastSwapParams";
    }

    void WalletClient::loadSwapParams()
    {
        ByteBuffer params;
        m_walletDB->getBlob(SWAP_PARAMS_NAME, params);
        onSwapParamsLoaded(params);
    }

    void WalletClient::storeSwapParams(const ByteBuffer& params)
    {
        m_walletDB->setVarRaw(SWAP_PARAMS_NAME, params.data(), params.size());
    }

#endif

    void WalletClient::cancelTx(const TxID& id)
    {
        auto w = m_wallet.lock();
        if (w)
        {
            w->CancelTransaction(id);
        }
    }

    void WalletClient::deleteTx(const TxID& id)
    {
        auto w = m_wallet.lock();
        if (w)
        {
            w->DeleteTransaction(id);
        }
    }

    void WalletClient::getCoinsByTx(const TxID& id)
    {
        onCoinsByTx(m_walletDB->getCoinsByTx(id));
    }

    void WalletClient::saveAddress(const WalletAddress& address, bool bOwn)
    {
        m_walletDB->saveAddress(address);
    }

    void WalletClient::changeCurrentWalletIDs(const WalletID& senderID, const WalletID& receiverID)
    {
        onChangeCurrentWalletIDs(senderID, receiverID);
    }

    void WalletClient::generateNewAddress()
    {
        try
        {
            WalletAddress address = storage::createAddress(*m_walletDB, m_keyKeeper);

            onGeneratedNewAddress(address);
        }
        catch (const TrezorKeyKeeper::DeviceNotConnected&)
        {
            onNoDeviceConnected();
        }
        catch (const CannotGenerateSecretException&)
        {
            onNewAddressFailed();
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::deleteAddress(const WalletID& id)
    {
        try
        {
            auto pVal = m_walletDB->getAddress(id);
            if (pVal)
            {
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

    void WalletClient::updateAddress(const WalletID& id, const std::string& name, WalletAddress::ExpirationStatus status)
    {
        try
        {
            auto addr = m_walletDB->getAddress(id);

            if (addr)
            {
                if (addr->isOwn() &&
                    status != WalletAddress::ExpirationStatus::AsIs)
                {
                    addr->setExpiration(status);
                }
                addr->setLabel(name);
                m_walletDB->saveAddress(*addr);
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
        if (auto s = m_nodeNetwork.lock())
        {
            if (!(s->setNodeAddress(addr)))
            {
                LOG_ERROR() << "Unable to resolve node address: " << addr;
                onWalletError(ErrorType::HostResolvedError);
            }
        }
        else
        {
            io::Address address;
            if (address.resolve(addr.c_str()))
            {
                m_initialNodeAddrStr = addr;
            }
            else
            {
                LOG_ERROR() << "Unable to resolve node address: " << addr;
                onWalletError(ErrorType::HostResolvedError);
            }
        }
    }

    void WalletClient::changeWalletPassword(const SecString& pass)
    {
        m_walletDB->changePassword(pass);
    }

    void WalletClient::getNetworkStatus()
    {
        if (m_walletError.is_initialized() && !isConnected())
        {
            onWalletError(*m_walletError);
            return;
        }

        onNodeConnectionChanged(isConnected());
    }

    void WalletClient::rescan()
    {
        try
        {
            assert(!m_wallet.expired());
            auto s = m_wallet.lock();
            if (s)
            {
                s->Rescan();
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...)
        {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void WalletClient::exportPaymentProof(const TxID& id)
    {
        onPaymentProofExported(id, storage::ExportPaymentProof(*m_walletDB, id));
    }

    void WalletClient::checkAddress(const std::string& addr)
    {
        io::Address nodeAddr;

        onAddressChecked(addr, nodeAddr.resolve(addr.c_str()));
    }

    void WalletClient::importRecovery(const std::string& path)
    {
        try
        {
            m_walletDB->ImportRecovery(path, *this);
            return;
        }
        catch (const std::runtime_error& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) 
        {
            LOG_UNHANDLED_EXCEPTION();
        }
        onWalletError(ErrorType::ImportRecoveryError);
    }

    void WalletClient::importDataFromJson(const std::string& data)
    {
        auto isOk = storage::ImportDataFromJson(*m_walletDB, m_keyKeeper, data.data(), data.size());

        onImportDataFromJson(isOk);
    }

    void WalletClient::exportDataToJson()
    {
        auto data = storage::ExportDataToJson(*m_walletDB);

        onExportDataToJson(data);
    }

    void WalletClient::exportTxHistoryToCsv()
    {
        auto data = storage::ExportTxHistoryToCsv(*m_walletDB);

        onExportTxHistoryToCsv(data);   
    }

    bool WalletClient::OnProgress(uint64_t done, uint64_t total)
    {
        onImportRecoveryProgress(done, total);
        return true;
    }

    WalletStatus WalletClient::getStatus() const
    {
        WalletStatus status;
        storage::Totals totalsCalc(*m_walletDB);
        const auto& totals = totalsCalc.GetTotals(Zero);

        status.available = totals.Avail;
        status.receivingIncoming = totals.ReceivingIncoming;
        status.receivingChange = totals.ReceivingChange;
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
        m_walletDB->visitCoins([&utxos](const Coin& c)->bool
            {
                utxos.push_back(c);
                return true;
            });
        return utxos;
    }

    void WalletClient::onNodeConnectionFailed(const proto::NodeConnection::DisconnectReason& reason)
    {
        // reason -> ErrorType
        if (proto::NodeConnection::DisconnectReason::ProcessingExc == reason.m_Type)
        {
            m_walletError = getWalletError(reason.m_ExceptionDetails.m_ExceptionType);
            onWalletError(*m_walletError);
            return;
        }

        if (proto::NodeConnection::DisconnectReason::Io == reason.m_Type)
        {
            m_walletError = getWalletError(reason.m_IoError);
            onWalletError(*m_walletError);
            return;
        }

        LOG_ERROR() << "Unprocessed error: " << reason;
    }

    void WalletClient::onNodeConnectedStatusChanged(bool isNodeConnected)
    {
        if (isNodeConnected)
        {
            ++m_connectedNodesCount;
        }
        else if (m_connectedNodesCount)
        {
            --m_connectedNodesCount;
        }

        onNodeConnectionChanged(isConnected());
    }

    void WalletClient::updateClientState()
    {
        if (auto w = m_wallet.lock(); w)
        {
            postFunctionToClientContext([this, currentHeight = m_walletDB->getCurrentHeight(), count = w->GetUnsafeActiveTransactionsCount()]()
            {
                m_currentHeight = currentHeight;
                m_unsafeActiveTxCount = count;
            });
        }
    }
    void WalletClient::updateClientTxState()
    {
        if (auto w = m_wallet.lock(); w)
        {
            postFunctionToClientContext([this, count = w->GetUnsafeActiveTransactionsCount()]()
            {
                m_unsafeActiveTxCount = count;
            });
        }
    }

    void WalletClient::updateConnectionTrust(bool trustedConnected)
    {
        if (trustedConnected)
        {
            ++m_trustedConnectionCount;
        }
        else if (m_trustedConnectionCount)
        {
            --m_trustedConnectionCount;
        }

        postFunctionToClientContext([this, isTrusted = m_trustedConnectionCount > 0 && m_trustedConnectionCount == m_connectedNodesCount]()
        {
            m_isConnectionTrusted = isTrusted;
        });
    }

    bool WalletClient::isConnected() const
    {
        return m_connectedNodesCount > 0;
    }
}
