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

#include "wallet.h"
#include <boost/uuid/uuid.hpp>

#include "core/ecc_native.h"
#include "core/block_crypt.h"
#include "core/shielded.h"
#include "core/treasury.h"
#include "utility/logger.h"
#include "utility/helpers.h"
#include "simple_transaction.h"
#include "contract_transaction.h"
#include "strings_resources.h"
#include "assets_utils.h"

#include <algorithm>
#include <random>
#include <iomanip>
#include <numeric>
#include <queue>

namespace beam::wallet
{
    using namespace std;
    using namespace ECC;

    namespace
    {
        bool ApplyTransactionParameters(BaseTransaction::Ptr tx, const PackedTxParameters& parameters, bool isInternalSource)
        {
            bool txChanged = false;
            SubTxID subTxID = kDefaultSubTxID;
            for (const auto& p : parameters)
            {
                if (p.first == TxParameterID::SubTxIndex)
                {
                    // change subTxID
                    Deserializer d;
                    d.reset(p.second.data(), p.second.size());
                    d& subTxID;
                    continue;
                }

                if (!isInternalSource && !tx->IsTxParameterExternalSettable(p.first, subTxID))
                {
                    BEAM_LOG_WARNING() << tx->GetTxID() << "Attempt to set internal tx parameter: " << static_cast<int>(p.first);
                    continue;
                }

                txChanged |= tx->GetWalletDB()->setTxParameter(tx->GetTxID(), subTxID, p.first, p.second, true, isInternalSource);
            }
            return txChanged;
        }
    }

    // @param address as string
    // Returns whether the address is a valid SBBS address i.e. a point on an ellyptic curve or a valid token
    bool CheckReceiverAddress(const std::string& addr)
    {
        auto p = ParseParameters(addr);
        return !!p;
    }

    TxParameters ProcessReceiverAddress(const TxParameters& parameters, IWalletDB::Ptr walletDB)
    {
        const auto& peerAddr = parameters.GetParameter<WalletID>(TxParameterID::PeerAddr);
        if (!peerAddr)
            return parameters;

        // if there is no parameter default behaviour is to save address or update it, for historical reasons
        auto savePeerAddressParam = parameters.GetParameter<bool>(TxParameterID::SavePeerAddress);
        bool savePeerAddress = !savePeerAddressParam || *savePeerAddressParam == true;

        auto receiverAddr = walletDB->getAddress(*peerAddr);
        if (receiverAddr)
        {
            if (receiverAddr->isOwn() && receiverAddr->isExpired())
            {
                BEAM_LOG_ERROR() << "Can't send to the expired address.";
                throw ReceiverAddressExpiredException();
            }

            if (savePeerAddress)
            {
                // update address comment if changed
                if (auto message = parameters.GetParameter<ByteBuffer>(TxParameterID::Message); message)
                {
                    auto messageStr = std::string(message->begin(), message->end());
                    if (messageStr != receiverAddr->m_label)
                    {
                        receiverAddr->m_label = messageStr;
                        walletDB->saveAddress(*receiverAddr);
                    }
                }
            }

            TxParameters temp{ parameters };
            temp.SetParameter(TxParameterID::IsSelfTx, receiverAddr->isOwn());
            return temp;
        }

        if (savePeerAddress)
        {
            WalletAddress address;
            address.m_BbsAddr = *peerAddr;
            address.m_createTime = getTimestamp();
            if (auto message = parameters.GetParameter<ByteBuffer>(TxParameterID::Message); message)
            {
                address.m_label = std::string(message->begin(), message->end());
            }
            if (auto ep = parameters.GetParameter<PeerID>(TxParameterID::PeerEndpoint); ep)
            {
                address.m_Endpoint = *ep;
            }
            walletDB->saveAddress(address);
        }

        TxParameters temp{parameters};
        temp.SetParameter(TxParameterID::IsSelfTx, false);
        return temp;
    }
    
    Wallet::Wallet(IWalletDB::Ptr walletDB, TxCompletedAction&& action, UpdateCompletedAction&& updateCompleted)
        : m_WalletDB{ walletDB }
        , m_TxCompletedAction{ std::move(action) }
        , m_UpdateCompleted{ std::move(updateCompleted) }
        , m_LastSyncTotal(0)
        , m_OwnedNodesOnline(0)
    {
        assert(walletDB);
        _myThread = std::this_thread::get_id();

        // the only default type of transaction
        RegisterTransactionType(TxType::Simple, make_unique<SimpleTransaction::Creator>(m_WalletDB));
        RegisterTransactionType(TxType::Contract, make_unique<ContractTransaction::Creator>(m_WalletDB));
        m_IsTreasuryHandled = storage::isTreasuryHandled(*m_WalletDB);
        m_Extra.m_ShieldedOutputs = m_WalletDB->get_ShieldedOuts();
    }

    Wallet::~Wallet()
    {
        CleanupNetwork();
    }

    void Wallet::CleanupNetwork()
    {
        // clear all requests
#define THE_MACRO(type) \
                while (!m_Pending##type.empty()) \
                    DeleteReq(*m_Pending##type.begin());

        WalletFlyClientRequests_All(THE_MACRO)
#undef THE_MACRO

        m_MessageEndpoints.clear();
        m_NodeEndpoint = nullptr;
    }

    // Fly client implementation
    void Wallet::get_Kdf(Key::IKdf::Ptr& pKdf)
    {
        pKdf = m_WalletDB->get_MasterKdf();
    }

    void Wallet::get_OwnerKdf(Key::IPKdf::Ptr& ownerKdf)
    {
        ownerKdf = m_WalletDB->get_OwnerKdf();
    }

    // Implementation of the FlyClient protocol method
    // @id : PeerID - peer id of the node
    // bUp : bool - flag indicating that node is online
    void Wallet::OnOwnedNode(const PeerID& id, bool bUp)
    {
        if (bUp)
        {
            if (!m_OwnedNodesOnline++) // on first connection to the node
            {
                AbortBodiesRequests();
                ResetCommitmentsCache();
                RequestEvents(); // maybe time to refresh UTXOs
            }
        }
        else
        {
            assert(m_OwnedNodesOnline); // check that m_OwnedNodesOnline is positive number
            if (!--m_OwnedNodesOnline)
            {
                AbortEvents();
            }
        }

        for (const auto sub : m_subscribers)
        {
            sub->onOwnedNode(id, bUp);
        }
    }

    Block::SystemState::IHistory& Wallet::get_History()
    {
        return m_WalletDB->get_History();
    }

    void Wallet::SetNodeEndpoint(proto::FlyClient::INetwork::Ptr nodeEndpoint)
    {
        m_NodeEndpoint = std::move(nodeEndpoint);
    }

    proto::FlyClient::INetwork::Ptr  Wallet::GetNodeEndpoint() const
    {
        return m_NodeEndpoint;
    }

    void Wallet::AddMessageEndpoint(IWalletMessageEndpoint::Ptr endpoint)
    {
        m_MessageEndpoints.insert(endpoint);
    }

    // Rescan the blockchain for UTXOs and shielded coins
    void Wallet::Rescan()
    {
        AbortEvents();
        AbortBodiesRequests();
        // We save all Incoming coins of active transactions and
        // restore them after clearing db. This will save our outgoing & available amounts
        std::vector<Coin> ocoins;
        // do the same thing with shielded coins which are in progress
        std::vector<ShieldedCoin> shieldedCoins;
        for (const auto& tx : m_ActiveTransactions)
        {
            const auto& txocoins = m_WalletDB->getCoinsCreatedByTx(tx.first);
            ocoins.insert(ocoins.end(), txocoins.begin(), txocoins.end());
            auto shieldedCoin = m_WalletDB->getShieldedCoin(tx.first);

            // save newly created coins only to be able to accomplish the transaction,
            // all the others should be able to be restored from blockchain
            if (shieldedCoin && shieldedCoin->m_createTxId && *shieldedCoin->m_createTxId == tx.first)
            {
                shieldedCoins.push_back(*shieldedCoin);
            }
        }

        m_WalletDB->clearCoins();
        m_WalletDB->clearShieldedCoins();

        // Restore Incoming coins of active transactions
        m_WalletDB->saveCoins(ocoins);
        // Restore shielded coins
        for (const auto& sc : shieldedCoins)
        {
            m_WalletDB->saveShieldedCoin(sc);
        }

        storage::setNextEventHeight(*m_WalletDB, 0);
        m_WalletDB->deleteEventsFrom(Rules::HeightGenesis - 1);
        ResetCommitmentsCache();
        SetTreasuryHandled(false);
        if (!m_OwnedNodesOnline)
        {
            storage::setNeedToRequestBodies(*m_WalletDB, true); // temporarily enable bodies requests
        }
        RequestBodies();
        RequestEvents();
    }

    void Wallet::RegisterTransactionType(TxType type, BaseTransaction::Creator::Ptr creator)
    {
        m_TxCreators[type] = std::move(creator);
    }

    TxID Wallet::StartTransaction(const TxParameters& parameters)
    {
        auto tx = ConstructTransactionFromParameters(parameters);
        if (!tx)
        {
            throw FailToStartNewTransactionException();
        }
        ProcessTransaction(tx);
        return *parameters.GetTxID();
    }

    void Wallet::ProcessTransaction(wallet::BaseTransaction::Ptr tx)
    {
        MakeTransactionActive(tx);
        UpdateTransaction(tx->GetTxID());
    }

    void Wallet::ResumeTransaction(const TxDescription& tx)
    {
        if (tx.canResume() && m_ActiveTransactions.find(tx.m_txId) == m_ActiveTransactions.end())
        {
            auto t = ConstructTransaction(tx.m_txId, tx.m_txType);
            if (t)
            {
                MakeTransactionActive(t);
                UpdateOnSynced(t);
            }
        }
    }

    void Wallet::VisitActiveTransaction(const TxVisitor& visitor)
    {
        for(const auto& it: m_ActiveTransactions)
        {
            visitor(it.first, it.second);
        }
    }

    void Wallet::ResumeAllTransactions()
    {
        auto func = [this](const auto& tx)
        {
            ResumeTransaction(tx);
            return true;
        };
        TxListFilter filter;
        m_WalletDB->visitTx(func, filter);
    }

    bool Wallet::IsWalletInSync() const
    {
        Block::SystemState::ID stateID;
        ZeroObject(stateID);
        m_WalletDB->getSystemStateID(stateID);
        if (stateID.m_Height < Rules::HeightGenesis)
            return false;

        Block::SystemState::Full state;
        m_WalletDB->get_History().get_At(state, stateID.m_Height);
        return IsValidTimeStamp(state.m_TimeStamp);
    }

    Height Wallet::get_TipHeight() const
    {
        Block::SystemState::Full s;
        get_tip(s);
        return s.m_Height;
    }

    void Wallet::DoInSyncedWallet(OnSyncAction&& action)
    {
        bool bSynced = !SyncRemains() && IsNodeInSync();

        if (bSynced)
        {
            AsyncContextHolder holder(*this);
            action();
        }
        else
        {
            m_SyncActionsQueue.push(std::move(action));
        }
    }

    size_t Wallet::GetUnsafeActiveTransactionsCount() const
    {
        return std::count_if(m_ActiveTransactions.begin(), m_ActiveTransactions.end(), [](const auto& p)
            {
                return p.second && !p.second->IsInSafety();
            });
    }

    void Wallet::OnAsyncStarted()
    {
        if (m_AsyncUpdateCounter == 0)
        {
            BEAM_LOG_VERBOSE() << "Async update started!";
        }
        ++m_AsyncUpdateCounter;
    }

    void Wallet::OnAsyncFinished()
    {
        if (--m_AsyncUpdateCounter == 0)
        {
            BEAM_LOG_VERBOSE() << "Async update finished!";
            if (m_UpdateCompleted)
            {
                m_UpdateCompleted();
            }
        }
    }

    void Wallet::on_tx_completed(const TxID& id)
    {
        // Note: the passed TxID is (most probably) the member of the transaction, 
        // which we, most probably, are going to erase from the map, which can potentially delete it.
        // Make sure we either copy the txID, or prolong the lifetime of the tx.
        TxID txID = id; // copy
        BaseTransaction::Ptr pGuard;

        auto it = m_ActiveTransactions.find(txID);
        if (it != m_ActiveTransactions.end())
        {
            pGuard.swap(it->second);
            m_ActiveTransactions.erase(it);
            m_NextTipTransactionToUpdate.erase(pGuard);

            pGuard->FreeResources();
        }

        if (m_TxCompletedAction)
        {
            m_TxCompletedAction(txID);
        }
    }

    void Wallet::on_tx_failed(const TxID& txID)
    {
        on_tx_completed(txID);
    }

#define WALLET_REQUEST_Single(type) \
    bool Wallet::MyRequest##type::operator < (const MyRequest##type& x) const { return false; }

    WALLET_REQUEST_Single(Events)
    WALLET_REQUEST_Single(StateSummary)
    WALLET_REQUEST_Single(ShieldedOutputsAt)
    WALLET_REQUEST_Single(BodyPack)
    WALLET_REQUEST_Single(Body)
    WALLET_REQUEST_Single(AssetsListAt)


    bool Wallet::MyRequestUtxo::operator < (const MyRequestUtxo& x) const
    {
        return m_Msg.m_Utxo < x.m_Msg.m_Utxo;
    }

    bool Wallet::MyRequestKernel::operator < (const MyRequestKernel& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestKernel2::operator < (const MyRequestKernel2& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestAsset::operator < (const MyRequestAsset& x) const
    {
        if (m_TxID < x.m_TxID) {
            return true;
        }

        if (m_Msg.m_AssetID < x.m_Msg.m_AssetID) {
            return true;
        }

        if (m_Msg.m_Owner < x.m_Msg.m_Owner) {
            return true;
        }

        return false;
    }

    bool Wallet::MyRequestTransaction::operator < (const MyRequestTransaction& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestProofShieldedOutp::operator < (const MyRequestProofShieldedOutp& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestShieldedList::operator < (const MyRequestShieldedList& x) const
    {
        return m_TxID < x.m_TxID;
    }

    void Wallet::RequestHandler::OnComplete(Request& r)
    {
        auto n = get_ParentObj().SyncRemains();

        switch (r.get_Type())
        {
#define THE_MACRO(type) \
        case Request::Type::type: \
            { \
                MyRequest##type& x = static_cast<MyRequest##type&>(r); \
                get_ParentObj().DeleteReq(x); \
                get_ParentObj().OnRequestComplete(x); \
            } \
            break;

            WalletFlyClientRequests_All(THE_MACRO)
#undef THE_MACRO

        default:
            assert(false);
        }

        if (n)
            get_ParentObj().CheckSyncDone();
    }

    // Implementation of the INegotiatorGateway::confirm_kernel
    // @param txID : TxID - transaction id
    // @param kernelID : Merkle::Hash& - kernel id
    // @param subTxID : SubTxID - in case of the complex transaction, there could be sub transactions
    void Wallet::confirm_kernel(const TxID& txID, const Merkle::Hash& kernelID, SubTxID subTxID)
    {
        if (auto it = m_ActiveTransactions.find(txID); it != m_ActiveTransactions.end())
        {
            // check if we have already asked for kernel on given height
            Height lastUnconfirmedHeight = 0;
            if (it->second->GetParameter(TxParameterID::KernelUnconfirmedHeight, lastUnconfirmedHeight, subTxID) && lastUnconfirmedHeight > 0)
            {
                Block::SystemState::Full state;
                if (!get_tip(state) || state.m_Height == lastUnconfirmedHeight)
                {
                    UpdateOnNextTip(it->second);
                    return;
                }
            }

            MyRequestKernel::Ptr pVal(new MyRequestKernel);
            pVal->m_TxID = txID;
            pVal->m_SubTxID = subTxID;
            pVal->m_Msg.m_ID = kernelID;

            if (PostReqUnique(*pVal))
                BEAM_LOG_INFO() << txID << "[" << subTxID << "]" << " Get proof for kernel: " << pVal->m_Msg.m_ID;
        }
    }

    void Wallet::confirm_kernel_ex(const Merkle::Hash& kernelID, IConfirmCallback::Ptr&& pCallback)
    {
        MyRequestKernel::Ptr pVal(new MyRequestKernel);
        pVal->m_Msg.m_ID = kernelID;
        pVal->m_pCallback = std::move(pCallback);

        if (PostReq(*pVal))
        {
            BEAM_LOG_INFO() << " Get proof for kernel: " << kernelID;
        }
    }

    void Wallet::confirm_asset(const TxID& txID, const PeerID& ownerID, SubTxID subTxID)
    {
        MyRequestAsset::Ptr pVal(new MyRequestAsset);
        pVal->m_TxID = txID;
        pVal->m_SubTxID = subTxID;
        pVal->m_Msg.m_Owner = ownerID;
        pVal->m_Msg.m_AssetID = Asset::s_InvalidID;

        if (PostReqUnique(*pVal))
        {
           BEAM_LOG_INFO() << txID << "[" << subTxID << "]" << " Get proof for asset with the owner ID: " << ownerID;
        }
    }

    void Wallet::confirm_asset(const TxID& txID, Asset::ID assetId, SubTxID subTxID)
    {
        MyRequestAsset::Ptr pVal(new MyRequestAsset);
        pVal->m_TxID = txID;
        pVal->m_SubTxID = subTxID;
        pVal->m_Msg.m_Owner = Asset::s_InvalidOwnerID;
        pVal->m_Msg.m_AssetID = assetId;

        if (PostReqUnique(*pVal))
        {
            BEAM_LOG_INFO() << txID << "[" << subTxID << "]" << " Get proof for asset with id: " << assetId;
        }
    }

    void Wallet::confirm_asset(const Asset::ID assetId)
    {
        MyRequestAsset::Ptr pVal(new MyRequestAsset);
        pVal->m_TxID = {0};
        pVal->m_SubTxID = 0;
        pVal->m_Msg.m_Owner = Asset::s_InvalidOwnerID;
        pVal->m_Msg.m_AssetID = assetId;

        if (PostReqUnique(*pVal))
        {
            BEAM_LOG_INFO() << "Get proof for asset with id: " << assetId;
        }
    }

    // Implementation of the INegotiatorGateway::get_kernel
    // @param txID : TxID - transaction id
    // @param kernelID : Merkle::Hash& - kernel id
    // @param subTxID : SubTxID - in case of the complex transaction, there could be sub transactions
    void Wallet::get_kernel(const TxID& txID, const Merkle::Hash& kernelID, SubTxID subTxID)
    {
        if (auto it = m_ActiveTransactions.find(txID); it != m_ActiveTransactions.end())
        {
            MyRequestKernel2::Ptr pVal(new MyRequestKernel2);
            pVal->m_TxID = txID;
            pVal->m_SubTxID = subTxID;
            auto& msg = pVal->m_Msg; // alias
            msg.m_Fetch = true;
            msg.m_ID = kernelID;

            if (PostReqUnique(*pVal))
            {
                BEAM_LOG_INFO() << txID << "[" << subTxID << "]" << " Get details for kernel: " << msg.m_ID;
            }
        }
    }

    // Implementation of the INegotiatorGateway::get_tip
    bool Wallet::get_tip(Block::SystemState::Full& state) const
    {
        return m_WalletDB->get_History().get_Tip(state);
    }

    // Implementation of the INegotiatorGateway::send_tx_params
    void Wallet::send_tx_params(const WalletID& peerID, const SetTxParameter& msg)
    {
        for (auto& endpoint : m_MessageEndpoints)
        {
            endpoint->Send(peerID, msg);
        }
    }

    // Implementation of the INegotiatorGateway::get_shielded_list
    void Wallet::get_shielded_list(const TxID& txId, TxoID startIndex, uint32_t count, ShieldedListCallback&& callback)
    {
        MyRequestShieldedList::Ptr pVal(new MyRequestShieldedList);
        pVal->m_callback = std::move(callback);
        pVal->m_TxID = txId;

        pVal->m_Msg.m_Id0 = startIndex;
        pVal->m_Msg.m_Count = count;

        if (PostReq(*pVal))
        {
            BEAM_LOG_INFO() << txId << " Get shielded list, start_index = " << startIndex << ", count = " << count;
        }
    }

    void Wallet::get_proof_shielded_output(const TxID& txId, const ECC::Point& serialPublic, ProofShildedOutputCallback&& callback)
    {
        MyRequestProofShieldedOutp::Ptr pVal(new MyRequestProofShieldedOutp);
        pVal->m_callback = std::move(callback);
        pVal->m_TxID = txId;

        pVal->m_Msg.m_SerialPub = serialPublic;

        if (PostReqUnique(*pVal))
        {
            BEAM_LOG_INFO() << txId << " Get proof of shielded output.";
        }
    }

    // Implementation of the INegotiatorGateway::UpdateOnNextTip
    void Wallet::UpdateOnNextTip(const TxID& txID)
    {
        auto it = m_ActiveTransactions.find(txID);
        if (it != m_ActiveTransactions.end())
        {
            UpdateOnNextTip(it->second);
        }
    }

    Wallet::VoucherManager::Request* Wallet::VoucherManager::CreateIfNew(const WalletID& trg)
    {
        Request::Target key;
        key.m_Value = trg;
        if (m_setTrg.end() != m_setTrg.find(key))
            return nullptr;

        ECC::Scalar::Native nonce;
        nonce.GenRandomNnz();

        Request* pReq = new Request;
        pReq->m_Target.m_Value = trg;
        m_setTrg.insert(pReq->m_Target);

        pReq->m_OwnAddr.m_Pk.FromSk(nonce);
        pReq->m_OwnAddr.SetChannelFromPk();

        get_ParentObj().Listen(pReq->m_OwnAddr, nonce);

        return pReq;
    }

    void Wallet::Listen(const WalletID& wid, const ECC::Scalar::Native& sk, IHandler* pH)
    {
        for (auto& p : m_MessageEndpoints)
            p->Listen(wid, sk, pH);
    }

    void Wallet::Unlisten(const WalletID& wid, IHandler* pH)
    {
        for (auto& p : m_MessageEndpoints)
            p->Unlisten(wid, pH);
    }

    void Wallet::Send(const WalletID& peerID, const Blob& b)
    {
        for (auto& p : m_MessageEndpoints)
            Cast::Down<IRawCommGateway>(*p).Send(peerID, b);
    }


    void Wallet::VoucherManager::Delete(Request& r)
    {
        get_ParentObj().Unlisten(r.m_OwnAddr);

        m_setTrg.erase(Request::Target::Set::s_iterator_to(r.m_Target));
        delete &r;
    }

    void Wallet::VoucherManager::DeleteAll()
    {
        while (!m_setTrg.empty())
            Delete(m_setTrg.begin()->get_ParentObj());
    }

    void Wallet::get_UniqueVoucher(const WalletID& peerID, const TxID&, boost::optional<ShieldedTxo::Voucher>& res)
    {
        auto count = m_WalletDB->getVoucherCount(peerID);
        const size_t VoucherCountThreshold = 5;

        if (count < VoucherCountThreshold)
        {
            VoucherManager::Request* pReq = m_VoucherManager.CreateIfNew(peerID);
            if (pReq)
                RequestVouchersFrom(peerID, pReq->m_OwnAddr, 30);
        }

        if (count > 0)
            res = m_WalletDB->grabVoucher(peerID);
    }

    void Wallet::SendSpecialMsg(const WalletID& peerID, SetTxParameter& msg)
    {
        memset0(&msg.m_TxID.front(), msg.m_TxID.size());
        send_tx_params(peerID, msg);
    }

    void Wallet::RequestVouchersFrom(const WalletID& peerID, const WalletID& myID, uint32_t nCount /* = 1 */)
    {
        SetTxParameter msg;
        msg.m_From = myID;
        msg.m_Type = TxType::VoucherRequest;

        msg.AddParameter((TxParameterID) 0, nCount);

        SendSpecialMsg(peerID, msg);
    }

    void Wallet::OnSpecialMsg(const WalletID& myID, const SetTxParameter& msg)
    {
        switch (msg.m_Type)
        {
        case TxType::VoucherRequest:
            {
                LOG_DEBUG() << "Got voucher request!";
                auto pKeyKeeper = m_WalletDB->get_KeyKeeper();
                if (!pKeyKeeper                                         // We can generate the ticket with OwnerKey, but can't sign it.
                 || !CanDetectCoins())                                  // The wallet has no ability to recognize received shielded coin
                {
                    BEAM_LOG_ERROR() << "Cannot send voucher" << TRACE(pKeyKeeper) << TRACE(CanDetectCoins());
                    FailVoucherRequest(msg.m_From, myID);
                    return; 
                }

                uint32_t nCount = 0;
                msg.GetParameter((TxParameterID) 0, nCount);

                if (!nCount)
                {
                    BEAM_LOG_ERROR() << "Cannot send voucher" << TRACE(nCount);
                    FailVoucherRequest(msg.m_From, myID);
                    return; //?!
                }

                auto address = m_WalletDB->getAddress(myID);
                if (!address.is_initialized() || !address->m_OwnID)
                    return;

                auto res = GenerateVoucherList(pKeyKeeper, address->m_OwnID, nCount);
                LOG_DEBUG() << "Generated voucher list, size: " << res.size();
                SetTxParameter msgOut;
                msgOut.m_Type = TxType::VoucherResponse;
                msgOut.m_From = myID;
                msgOut.AddParameter(TxParameterID::ShieldedVoucherList, res);

                SendSpecialMsg(msg.m_From, msgOut);

            }
            break;

        case TxType::VoucherResponse:
            {
                std::vector<ShieldedTxo::Voucher> res;
                msg.GetParameter(TxParameterID::ShieldedVoucherList, res);
                if (res.empty())
                {
                    BEAM_LOG_WARNING() << "Received an empty voucher list";
                    FailTxWaitingForVouchers(msg.m_From);
                    return;
                }

                auto address = m_WalletDB->getAddress(msg.m_From);
                if (!address.is_initialized())
                {
                    BEAM_LOG_WARNING() << "Received vouchers for unknown address: " << std::to_string(msg.m_From);
                    FailTxWaitingForVouchers(msg.m_From);
                    return;
                }

                if (!IsValidVoucherList(res, address->m_Endpoint))
                {
                    BEAM_LOG_WARNING() << "Invalid voucher list received";
                    FailTxWaitingForVouchers(msg.m_From);
                    return;
                }

                OnVouchersFrom(*address, myID, std::move(res));

            }
            break;

            case TxType::InstantSbbsMessage:
            {
                auto receiver = m_WalletDB->getAddress(myID);
                if (!receiver.is_initialized() || !receiver->m_OwnID)
                    return;

                size_t message_size = 0;
                msg.GetParameter((TxParameterID) 0, message_size);

                Timestamp timestamp;
                msg.GetParameter((TxParameterID) 1, timestamp);

                ByteBuffer message_bb;
                message_bb.reserve(message_size);
                msg.GetParameter((TxParameterID) 2, message_bb);
                std::string message(message_bb.begin(), message_bb.end());

                m_WalletDB->storeIM(timestamp, msg.m_From, myID, message, true, false);
            }
            break;

        default: // suppress watning
            break;
        }
    }

    std::vector<BaseTransaction::Ptr> Wallet::FindTxWaitingForVouchers(const WalletID& peerID) const
    {
        std::vector<BaseTransaction::Ptr> res;
        for (auto p : m_ActiveTransactions)
        {
            ShieldedTxo::Voucher voucher;
            WalletID txPeerID;
            const auto& tx = p.second;
            if (tx->GetType() == TxType::PushTransaction
                && tx->GetParameter<WalletID>(TxParameterID::PeerAddr, txPeerID)
                && peerID == txPeerID
                && !tx->GetParameter<ShieldedTxo::Voucher>(TxParameterID::Voucher, voucher))
            {
                res.push_back(tx);
            }
        }
        return res;
    }

    void Wallet::FailTxWaitingForVouchers(const WalletID& peerID)
    {
        auto transactions = FindTxWaitingForVouchers(peerID);
        for (auto tx : transactions)
        {
            tx->SetParameter(TxParameterID::FailureReason, TxFailureReason::CannotGetVouchers);
            UpdateTransaction(tx);
        }
    }

    void Wallet::FailVoucherRequest(const WalletID& peerID, const WalletID& myID)
    {
        SetTxParameter msgOut;
        msgOut.m_Type = TxType::VoucherResponse;
        msgOut.m_From = myID;
        msgOut.AddParameter(TxParameterID::FailureReason, TxFailureReason::CannotGetVouchers);

        SendSpecialMsg(peerID, msgOut);
    }

    void Wallet::OnVouchersFrom(const WalletAddress& addr, const WalletID& ownAddr, std::vector<ShieldedTxo::Voucher>&& res)
    {
        VoucherManager::Request::Target key;
        key.m_Value = addr.m_BbsAddr;

        VoucherManager::Request::Target::Set::iterator it = m_VoucherManager.m_setTrg.find(key);
        if (m_VoucherManager.m_setTrg.end() == it)
            return;
        VoucherManager::Request& r = it->get_ParentObj();

        if (r.m_OwnAddr != ownAddr)
            return;

        m_VoucherManager.Delete(r);

        for (const auto& v : res)
            m_WalletDB->saveVoucher(v, addr.m_BbsAddr);

        auto transactions = FindTxWaitingForVouchers(addr.m_BbsAddr);
        for (auto tx : transactions)
        {
            UpdateTransaction(tx);
        }
    }

    void Wallet::OnWalletMessage(const WalletID& myID, const SetTxParameter& msg)
    {
        if (memis0(&msg.m_TxID.front(), msg.m_TxID.size()))
        {
            // special command/request
            try
            {
                OnSpecialMsg(myID, msg);
            }
            catch (const std::exception& exc)
            {
                BEAM_LOG_WARNING() << "Special msg failed: " << exc.what();
            }
        }
        else
        {
            OnTransactionMsg(myID, msg);
        }
    }

    void Wallet::OnRequestComplete(MyRequestTransaction& r)
    {
        LOG_DEBUG() << r.m_TxID << "[" << r.m_SubTxID << "]" << " register status " << static_cast<uint32_t>(r.m_Res.m_Value);
        if (!r.m_Res.m_ExtraInfo.empty())
            BEAM_LOG_WARNING() << "Extra info: " << r.m_Res.m_ExtraInfo;

        auto it = m_ActiveTransactions.find(r.m_TxID);
        if (it != m_ActiveTransactions.end())
        {
            it->second->SetParameter(TxParameterID::TransactionRegistered, r.m_Res.m_Value, r.m_SubTxID);
            it->second->SetParameter(TxParameterID::TransactionRegisteredExtraInfo, r.m_Res.m_ExtraInfo, r.m_SubTxID);
            it->second->SetParameter(TxParameterID::TransactionRegisteredInternal, r.m_Res.m_Value, r.m_SubTxID);
            UpdateTransaction(r.m_TxID);
        }
    }

    bool Wallet::CanCancelTransaction(const TxID& txId) const
    {
        if (auto it = m_ActiveTransactions.find(txId); it != m_ActiveTransactions.end())
        {
            return it->second->CanCancel();
        }
        return false;
    }

    void Wallet::CancelTransaction(const TxID& txId)
    {
        BEAM_LOG_INFO() << txId << " Canceling tx";

        if (auto it = m_ActiveTransactions.find(txId); it != m_ActiveTransactions.end())
        {
            it->second->Cancel();
        }
        else
        {
            BEAM_LOG_WARNING() << "Transaction already inactive";
        }
    }

    void Wallet::DeleteTransaction(const TxID& txId)
    {
        BEAM_LOG_INFO() << "deleting tx " << txId;
        if (auto it = m_ActiveTransactions.find(txId); it == m_ActiveTransactions.end())
        {
            m_WalletDB->deleteTx(txId);
        }
        else
        {
            BEAM_LOG_WARNING() << "Cannot delete running transaction";
        }
    }

    void Wallet::UpdateTransaction(const TxID& txID)
    {
        auto it = m_ActiveTransactions.find(txID);
        if (it != m_ActiveTransactions.end())
        {
            UpdateTransaction(it->second);
        }
        else
        {
            LOG_DEBUG() << txID << " Unexpected event";
        }
    }

    void Wallet::ConfirmAsset(Asset::ID assetId)
    {
        confirm_asset(assetId);
    }

    void Wallet::UpdateTransaction(BaseTransaction::Ptr tx)
    {
        DoInSyncedWallet([this, tx]() { UpdateActiveTransaction(tx); });
    }

    void Wallet::UpdateActiveTransaction(BaseTransaction::Ptr tx)
    {
        auto it = m_ActiveTransactions.find(tx->GetTxID());
        if (it != m_ActiveTransactions.end())
        {
            tx->Update();
        }
    }

    void Wallet::UpdateOnSynced(BaseTransaction::Ptr tx)
    {
        m_SyncActionsQueue.push([this, tx]()
        {
             UpdateActiveTransaction(tx);
        });
    }

    void Wallet::UpdateOnNextTip(BaseTransaction::Ptr tx)
    {
        m_NextTipTransactionToUpdate.insert(tx);
    }

    void Wallet::OnRequestComplete(MyRequestUtxo& r)
    {
        if (r.m_Res.m_Proofs.empty())
            return; // Right now nothing is concluded from empty proofs

        const auto& proof = r.m_Res.m_Proofs.front(); // Currently - no handling for multiple coins for the same commitment.
        CacheCommitment(r.m_Msg.m_Utxo, proof.m_State.m_Maturity, true);
        // we don't know the real height, but it'll be used for logging only. For standard outputs maturity and height are the same
        ProcessEventUtxo(r.m_CoinID, proof.m_State.m_Maturity, proof.m_State.m_Maturity, true, {});
    }

    void Wallet::OnRequestComplete(MyRequestKernel& r)
    {
        if (!r.m_Res.m_Proof.empty())
            m_WalletDB->get_History().AddStates(&r.m_Res.m_Proof.m_State, 1); // why not?

        if (r.m_pCallback)
        {
            r.m_pCallback->OnDone(r.m_Res.m_Proof.empty() ? nullptr : &r.m_Res.m_Proof.m_State.m_Height);
            return;
        }

        auto it = m_ActiveTransactions.find(r.m_TxID);
        if (m_ActiveTransactions.end() == it)
            return;

        auto tx = it->second;
        if (!r.m_Res.m_Proof.empty())
        {
            if (tx->SetParameter(TxParameterID::KernelProofHeight, r.m_Res.m_Proof.m_State.m_Height, r.m_SubTxID))
            {
                UpdateTransaction(tx);
            }
        }
        else
        {
            Block::SystemState::Full sTip;
            get_tip(sTip);
            tx->SetParameter(TxParameterID::KernelUnconfirmedHeight, sTip.m_Height, r.m_SubTxID);
            UpdateTransaction(tx);
        }
    }

    void Wallet::OnRequestComplete(MyRequestAsset& req)
    {
        BaseTransaction::Ptr tx;
        const auto it = m_ActiveTransactions.find(req.m_TxID);
        if (m_ActiveTransactions.end() != it)
        {
           tx = it->second;
        }

        Block::SystemState::Full sTip;
        get_tip(sTip);

        std::string msgPrefix;
        if (!std::all_of(req.m_TxID.begin(), req.m_TxID.end(), [](const auto& v) { return v == 0; })) // any non-zero val in array
        {
            stringstream ss;
            ss << req.m_TxID << "[" << req.m_SubTxID << "]";
            msgPrefix = ss.str();
        }

        if (!req.m_Res.m_Proof.empty())
        {
            const auto& info  = req.m_Res.m_Info;
            const auto height = sTip.m_Height;
            ProcessAssetInfo(info, height, msgPrefix);

            if (tx)
            {
                tx->SetParameter(TxParameterID::AssetConfirmedHeight, height, req.m_SubTxID);
                tx->SetParameter(TxParameterID::AssetInfoFull, info, req.m_SubTxID);
                tx->SetParameter(TxParameterID::AssetUnconfirmedHeight, Height(0), req.m_SubTxID);
                UpdateTransaction(tx);
            }
            else
            {
                storage::setTxParameter(*m_WalletDB, req.m_TxID, req.m_SubTxID, TxParameterID::AssetConfirmedHeight, height, true);
                storage::setTxParameter(*m_WalletDB, req.m_TxID, req.m_SubTxID, TxParameterID::AssetInfoFull, info, true);
                storage::setTxParameter(*m_WalletDB, req.m_TxID, req.m_SubTxID, TxParameterID::AssetUnconfirmedHeight, Height(0), true);
            }
        }
        else
        {
            const auto& assetId = req.m_Msg.m_AssetID;
            if (assetId != Asset::s_InvalidID)
            {
                m_WalletDB->dropAsset(assetId);
            }
            else
            {
                const auto& assetOwner = req.m_Msg.m_Owner;
                if (assetOwner != Asset::s_InvalidOwnerID)
                {
                    m_WalletDB->dropAsset(assetOwner);
                }
            }

            if (tx)
            {
                tx->SetParameter(TxParameterID::AssetConfirmedHeight, Height(0), req.m_SubTxID);
                tx->SetParameter(TxParameterID::AssetInfoFull, Asset::Full(), req.m_SubTxID);
                tx->SetParameter(TxParameterID::AssetUnconfirmedHeight, sTip.m_Height, req.m_SubTxID);
                UpdateTransaction(tx);
            }
            else
            {
                storage::setTxParameter(*m_WalletDB, req.m_TxID, req.m_SubTxID, TxParameterID::AssetConfirmedHeight, Height(0), true);
                storage::setTxParameter(*m_WalletDB, req.m_TxID, req.m_SubTxID, TxParameterID::AssetInfoFull, Asset::Full(), true);
                storage::setTxParameter(*m_WalletDB, req.m_TxID, req.m_SubTxID, TxParameterID::AssetUnconfirmedHeight, sTip.m_Height, true);
            }
        }
    }

    void Wallet::ProcessAssetInfo(const Asset::Full& info, Height height, const std::string& logPrefix)
    {
        m_WalletDB->saveAsset(info, height);
        BEAM_LOG_INFO() << logPrefix << (logPrefix.empty() ? "" : " ") << "Received proof for Asset with ID " << info.m_ID;

        if (Key::IKdf::Ptr maserKdf = m_WalletDB->get_MasterKdf())
        {
            std::string strMeta;
            info.m_Metadata.get_String(strMeta);

            if (beam::wallet::GetAssetOwnerID(maserKdf, strMeta) == info.m_Owner)
            {
                m_WalletDB->markAssetOwned(info.m_ID);
            }
        }
        else
        {
            BEAM_LOG_WARNING() << logPrefix << "Unable to get master key. Asset's " << info.m_ID << " ownership won't be checked.";
        }

        if (const auto wasset = m_WalletDB->findAsset(info.m_ID))
        {
            wasset->LogInfo(logPrefix);
        }
    }

    void Wallet::OnRequestComplete(MyRequestKernel2& r)
    {
        auto it = m_ActiveTransactions.find(r.m_TxID);
        if (m_ActiveTransactions.end() == it)
        {
            return;
        }
        auto tx = it->second;

        if (r.m_Res.m_Kernel)
        {
            tx->SetParameter(TxParameterID::Kernel, r.m_Res.m_Kernel, r.m_SubTxID);
            tx->SetParameter(TxParameterID::KernelProofHeight, r.m_Res.m_Height, r.m_SubTxID);
        }
        else
        {
            Block::SystemState::Full sTip;
            get_tip(sTip);
            tx->SetParameter(TxParameterID::KernelUnconfirmedHeight, sTip.m_Height, r.m_SubTxID);
        }
    }

    void Wallet::OnRequestComplete(MyRequestShieldedList& r)
    {
        r.m_callback(r.m_Msg.m_Id0, r.m_Msg.m_Count, r.m_Res);
    }

    void Wallet::OnRequestComplete(MyRequestProofShieldedOutp& r)
    {
        r.m_callback(r.m_Res); // either successful or not
    }

    void Wallet::OnRequestComplete(MyRequestStateSummary& r)
    {
        // TODO: save full response?
        m_WalletDB->set_ShieldedOuts(r.m_Res.m_ShieldedOuts);
        m_WalletDB->set_AidMax(r.m_Res.m_AssetsMax);
        if (!IsMobileNodeEnabled())
        {
            m_Extra.m_ShieldedOutputs = r.m_Res.m_ShieldedOuts;
        }
    }

    void Wallet::OnRequestComplete(MyRequestShieldedOutputsAt& r)
    {
        r.m_callback(r.m_Msg.m_Height, r.m_Res.m_ShieldedOuts);
    }

    void Wallet::OnRequestComplete(MyRequestAssetsListAt& r)
    {
        Height tipHeight = get_TipHeight();
        for (const auto& ai : r.m_Res)
        {
            ProcessAssetInfo(ai, tipHeight, "");
        }
        r.m_callback(std::move(r.m_Res));
    }

    struct Wallet::RecognizerHandler : NodeProcessor::Recognizer::IHandler
    {
        Wallet& m_Wallet;
        NodeProcessor::Account m_Account;
        RecognizerHandler(Wallet& wallet, const Key::IPKdf::Ptr& pKdf)
            : m_Wallet(wallet)
        {
            m_Account.m_pOwner = pKdf;
            m_Account.InitFromOwner();
            m_pAccount = &m_Account;
        }

        void OnEvent(Height h, const proto::Event::Base& evt) override
        {
            switch (evt.get_Type())
            {
            case proto::Event::Type::Utxo:
            {
                auto event = Cast::Up<const proto::Event::Utxo&>(evt);
                m_Wallet.ProcessEventUtxo(event, h);
            }break;
            case proto::Event::Type::Shielded:
            {
                auto event = Cast::Up<const proto::Event::Shielded&>(evt);
                m_Wallet.ProcessEventShieldedUtxo(event, h);
            }break;
            default:
                break;
            }
        }

        void AssetEvtsGetStrict(NodeDB::AssetEvt& event, Height h, uint32_t nKrnIdx) override
        {
        }

        void InsertEvent(const HeightPos& pos, const Blob& b, const Blob& k) override
        {
            m_Wallet.m_WalletDB->insertEvent(pos.m_Height, b, k);
        }
/*
        struct WalletDBWalkerEvent : NodeProcessor::Recognizer::WalkerEventBase
        {
            IWalletDB::Ptr m_WalletDB;
            struct Event
            {
                Height m_Height = 0;
                ByteBuffer m_Body;
            };
            std::queue<Event> m_Events;
            Event m_CurrentEvent;
            Blob m_Body;
            WalletDBWalkerEvent(IWalletDB::Ptr walletDB)
                : m_WalletDB(walletDB)
            {
            }
            void Find(const Blob& key)
            {
                m_WalletDB->visitEvents(0, key, [this](Height h, ByteBuffer&& b)
                {
                    m_Events.push({ h, std::move(b) });
                    return true;
                });
            }
            bool MoveNext() override
            {
                if (m_Events.empty())
                    return false;
                m_CurrentEvent = std::move(m_Events.front());
                m_Body = Blob(m_CurrentEvent.m_Body);
                m_Events.pop();
                return true;
            }
            const Blob& get_Body() const override
            {
                return m_Body;
            }
        };

        std::unique_ptr<NodeProcessor::Recognizer::WalkerEventBase> FindEvents(const Blob& key) override
        {
            auto w = std::make_unique<WalletDBWalkerEvent>(m_Wallet.m_WalletDB);
            w->Find(key);
            return w;
        }*/

        bool FindEvents(const Blob& key, NodeProcessor::Recognizer::IEventHandler& handler) override
        {
            bool bFound = false;

            m_Wallet.m_WalletDB->visitEvents(0, key, [&handler, &bFound](Height h, ByteBuffer&& b)
            {
                Blob body = b;

                if (!handler.OnEvent(h, body))
                    return true; // continue

                bFound = true;
                return false; // stop
            });

            return bFound;
        }
    };

    void Wallet::OnRequestComplete(MyRequestBodyPack& r)
    {
        RecognizerHandler h(*this, m_WalletDB->get_OwnerKdf());
        NodeProcessor::Recognizer recognizer(h, m_Extra);
        try 
        {
            Height startHeight = r.m_StartHeight;
            if (!r.m_Res.m_Bodies.empty())
            {
                RequestBodies(r.m_Msg.m_Height0, startHeight + r.m_Res.m_Bodies.size());
            }
            for (const auto& b : r.m_Res.m_Bodies)
            {
                ProcessBody(b, startHeight, recognizer);

                ++startHeight;
            }
            assert(GetEventsHeightNext() == startHeight);

            m_WalletDB->set_ShieldedOuts(m_Extra.m_ShieldedOutputs);
        }
        catch (const std::exception&)
        {
            return;
        }
    }

    void Wallet::OnRequestComplete(MyRequestBody& r)
    {
        RecognizerHandler h(*this, m_WalletDB->get_OwnerKdf());
        NodeProcessor::Recognizer recognizer(h, m_Extra);
        try
        {
            if (r.m_Height == 0)
            {
                if (!r.m_Res.m_Body.m_Eternal.empty())
                {
                    // handle treasury
                    const Blob& blob = r.m_Res.m_Body.m_Eternal;
                    Treasury::Data td;
                    if (!NodeProcessor::ExtractTreasury(blob, td))
                        return;

                    for (size_t iG = 0; iG < td.m_vGroups.size(); iG++)
                    {
                        recognizer.RecognizeBlock(td.m_vGroups[iG].m_Data, 0, false);
                    }
                }

                SetTreasuryHandled(true);
                RequestBodies(0, Rules::get().HeightGenesis);
                return;
            }

            ProcessBody(r.m_Res.m_Body, r.m_Height, recognizer);

            if (r.m_Height < r.m_Msg.m_Top.m_Height)
            {
                RequestBodies(r.m_Height, r.m_Height+1);
            }
        }
        catch (const std::exception&)
        {
            return;
        }
    }

    void Wallet::ProcessBody(const proto::BodyBuffers& b, Height h, NodeProcessor::Recognizer& recognizer)
    {
        Block::Body block;
        Deserializer der;
        der.reset(b.m_Perishable);

        der & block.m_vInputs;

        size_t nOuts = 0;
        der & nOuts;
        block.m_vOutputs.resize(nOuts);

        for (size_t i = 0; i < nOuts; i++)
        {
            auto& pOut = block.m_vOutputs[i];
            pOut = std::make_unique<Output>();

            yas::detail::loadRecovery(der, *pOut, h);
        }

        der.reset(b.m_Eternal);
        der& Cast::Down<TxVectors::Eternal>(block);
        PreprocessBlock(block);

        recognizer.m_Pos.m_Height = h;
        recognizer.RecognizeBlock(block, 0, false);
        SetEventsHeight(h);
        ++m_BlocksDone;
    }

    void Wallet::PreprocessBlock(TxVectors::Full& block)
    {
        // In this method we emulate work performed by NodeProcessor::HandleValidatedBlock
        CacheCommitments();

        for (auto& input : block.m_vInputs)
        {
            auto cit = m_Commitments.find(input->m_Commitment);
            if (cit != m_Commitments.end())
            {
                input->m_Internal.m_Maturity = cit->second;
            }
        }

        // remove asset kernels, we don't support them
        auto& kernels = block.m_vKernels;
        kernels.erase(std::remove_if(kernels.begin(), kernels.end(), [](const auto& k)
        {
            switch (k->get_Subtype())
            {
            case TxKernel::Subtype::AssetCreate:
            case TxKernel::Subtype::AssetDestroy:
            case TxKernel::Subtype::AssetEmit:
                return true;
            default:
                return false;
            }
        }), kernels.end());
    }

    void Wallet::RequestBodies()
    {
        if (!IsMobileNodeEnabled())
            return;

        Block::SystemState::Full tip;
        get_tip(tip);

        if (!storage::isTreasuryHandled(*m_WalletDB))
        {
            m_RequestedBlocks = tip.m_Height;
            RequestTreasury();
        }
        else
        {
            Height nextEvent = GetEventsHeightNext();
            if (nextEvent) 
            {
                --nextEvent;
                if (!m_RequestedBlocks)
                {
                    m_RequestedBlocks = tip.m_Height - nextEvent;
                }
                else
                {
                    // new tip
                    ++m_RequestedBlocks;
                }
            }
            else
            {
                m_RequestedBlocks = tip.m_Height;
            }
            RequestBodies(nextEvent, nextEvent + 1);
        }
    }

    void Wallet::RequestTreasury()
    {
        if (!IsMobileNodeEnabled())
            return;

        m_Extra.m_ShieldedOutputs = 0;
        m_WalletDB->set_ShieldedOuts(0);

        MyRequestBody::Ptr pReq(new MyRequestBody);

        pReq->m_Msg.m_FlagP = proto::BodyBuffers::Full;
        pReq->m_Msg.m_FlagE = proto::BodyBuffers::Full;

        pReq->m_Height = 0;

        PostReqUnique(*pReq);
    }

    void Wallet::RequestBodies(Height currentHeight, Height startHeight)
    {
        if (!IsMobileNodeEnabled())
            return;

        if (!m_PendingBodyPack.empty() || !m_PendingBody.empty())
            return;

        Block::SystemState::Full newTip;
        m_WalletDB->get_History().get_Tip(newTip);

        if (startHeight > newTip.m_Height)
            return;

        Height hCountExtra = newTip.m_Height - startHeight;
        if (hCountExtra)
        {
            MyRequestBodyPack::Ptr pReq(new MyRequestBodyPack);

            proto::GetBodyPack& msg = pReq->m_Msg;
            msg.m_FlagP = proto::BodyBuffers::Recovery1;
            msg.m_FlagE = proto::BodyBuffers::Full;

            newTip.get_ID(pReq->m_Msg.m_Top);

            Height r = Rules::get().MaxRollback;
            Height count = std::min(newTip.m_Height - currentHeight, r * 2);
            pReq->m_StartHeight = startHeight;
            msg.m_CountExtra = hCountExtra;
            msg.m_Height0 = currentHeight;
            msg.m_HorizonLo1 = newTip.m_Height - count;
            msg.m_HorizonHi1 = newTip.m_Height;

            PostReqUnique(*pReq);
        }
        else
        {
            MyRequestBody::Ptr pReq(new MyRequestBody);

            pReq->m_Msg.m_FlagP = proto::BodyBuffers::Recovery1;
            pReq->m_Msg.m_FlagE = proto::BodyBuffers::Full;

            newTip.get_ID(pReq->m_Msg.m_Top);
            pReq->m_Height = pReq->m_Msg.m_Top.m_Height;
            pReq->m_Msg.m_CountExtra = hCountExtra;

            PostReqUnique(*pReq);
        }
    }


    void Wallet::AbortBodiesRequests()
    {
        if (!m_PendingBodyPack.empty())
            DeleteReq(*m_PendingBodyPack.begin());

        if (!m_PendingBody.empty())
            DeleteReq(*m_PendingBody.begin());
    }

    void Wallet::RequestEvents()
    {
        if (!m_OwnedNodesOnline)
        {
            return;
        }

        Block::SystemState::Full sTip;
        m_WalletDB->get_History().get_Tip(sTip);

        Height h = GetEventsHeightNext();
        assert(h <= sTip.m_Height + 1);
        if (h > sTip.m_Height)
            return;

        if (!m_PendingEvents.empty())
        {
            if (m_PendingEvents.begin()->m_Msg.m_HeightMin == h)
                return; // already pending
            DeleteReq(*m_PendingEvents.begin());
        }

        MyRequestEvents::Ptr pReq(new MyRequestEvents);
        pReq->m_Msg.m_HeightMin = h;
        PostReqUnique(*pReq);
    }

    void Wallet::AbortEvents()
    {
        if (!m_PendingEvents.empty())
            DeleteReq(*m_PendingEvents.begin());
    }

    void Wallet::OnRequestComplete(MyRequestEvents& r)
    {
        struct MyParser
            :public proto::Event::IGroupParser
        {
            Wallet& m_This;
            MyParser(Wallet& x) :m_This(x) {}

            virtual void OnEventType(proto::Event::Shielded& evt) override
            {
                m_This.ProcessEventShieldedUtxo(evt, m_Height);
            }

            virtual void OnEventType(proto::Event::AssetCtl& evt) override
            {
                m_This.ProcessEventAsset(evt, m_Height);
            }

            virtual void OnEventType(proto::Event::Utxo& evt) override
            {
                m_This.ProcessEventUtxo(evt, m_Height);
            }

        } p(*this);

        uint32_t nCount = p.Proceed(r.m_Res.m_Events);

        if (nCount < proto::Event::s_Max)
        {
            Block::SystemState::Full sTip;
            m_WalletDB->get_History().get_Tip(sTip);

            SetEventsHeight(sTip.m_Height);
            if (!m_IsTreasuryHandled)
                SetTreasuryHandled(true); // to be able to switch to unsafe node
        }
        else
        {
            SetEventsHeight(p.m_Height);
            RequestEvents(); // maybe more events pending
        }
    }

    void Wallet::SetEventsHeight(Height h)
    {
        storage::setNextEventHeight(*m_WalletDB, h + 1); // we're actually saving the next
    }

    Height Wallet::GetEventsHeightNext() const
    {
        return storage::getNextEventHeight(*m_WalletDB);
    }

    void Wallet::ProcessEventUtxo(const proto::Event::Utxo& evt, Height h)
    {
        CoinID cid = evt.m_Cid;
        // filter-out false positives
        if (!m_WalletDB->IsRecoveredMatch(cid, evt.m_Commitment))
            return;

        bool bAdd = 0 != (proto::Event::Flags::Add & evt.m_Flags);
        CacheCommitment(evt.m_Commitment, evt.m_Maturity, bAdd);
        ProcessEventUtxo(cid, h, evt.m_Maturity, bAdd, evt.m_User);
    }

    void Wallet::ProcessEventUtxo(const CoinID& cid, Height h, Height hMaturity, bool bAdd, const Output::User& user)
    {
        Coin c;
        c.m_ID = cid;
        bool bExists = m_WalletDB->findCoin(c);
        c.m_maturity = hMaturity;

        const auto* data = Output::User::ToPacked(user);
        if (!memis0(data->m_TxID.m_pData, sizeof(TxID)))
        {
            c.m_createTxId.emplace();
            std::copy_n(data->m_TxID.m_pData, sizeof(TxID), c.m_createTxId->begin());
        }

        BEAM_LOG_INFO() << "CoinID: " << c.m_ID << " Maturity=" << hMaturity << (bAdd ? " Confirmed" : " Spent") << ", Height=" << h;

        if (bAdd)
        {
            std::setmin(c.m_confirmHeight, h); // in case of std utxo proofs - the event height may be bigger than actual utxo height

            // Check if this Coin participates in any active transaction
            // if it does and mark it as outgoing (bug: ux_504)
            for (const auto& [txid, txptr] : m_ActiveTransactions)
            {
                std::vector<Coin::ID> icoins;
                txptr->GetParameter(TxParameterID::InputCoins, icoins);
                if (std::find(icoins.begin(), icoins.end(), c.m_ID) != icoins.end())
                {
                    c.m_status = Coin::Status::Outgoing;
                    c.m_spentTxId = txid;
                    BEAM_LOG_INFO() << "CoinID: " << c.m_ID << " marked as Outgoing";
                }
            }
        }
        else
        {
            if (!bExists)
                return; // should alert!

            std::setmin(c.m_spentHeight, h); // reported spend height may be bigger than it actuall was (in case of macroblocks)
        }

        m_WalletDB->saveCoin(c);
    }

    void Wallet::ProcessEventAsset(const proto::Event::AssetCtl& assetCtl, Height h)
    {
        // TODO
    }

    void Wallet::ProcessEventShieldedUtxo(const proto::Event::Shielded& shieldedEvt, Height h)
    {
        auto shieldedCoin = m_WalletDB->getShieldedCoin(shieldedEvt.m_CoinID.m_Key);
        if (!shieldedCoin)
        {
            shieldedCoin = ShieldedCoin{};
        }

        shieldedCoin->m_CoinID = shieldedEvt.m_CoinID;
        shieldedCoin->m_TxoID = shieldedEvt.m_TxoID;

        bool isAdd = 0 != (proto::Event::Flags::Add & shieldedEvt.m_Flags);
        if (isAdd)
        {
            shieldedCoin->m_confirmHeight = std::min(shieldedCoin->m_confirmHeight, h);
        }
        else
        {
            shieldedCoin->m_spentHeight = std::min(shieldedCoin->m_spentHeight, h);
        }

        const auto* message = ShieldedTxo::User::ToPackedMessage(shieldedCoin->m_CoinID.m_User);
        if (!memis0(message->m_TxID.m_pData, sizeof(TxID)))
        {
            shieldedCoin->m_createTxId.emplace();
            std::copy_n(message->m_TxID.m_pData, sizeof(TxID), shieldedCoin->m_createTxId->begin());
        }

        // Check if this Coin participates in any active transaction
        for (const auto& [txid, txptr] : m_ActiveTransactions)
        {

            std::vector<IPrivateKeyKeeper2::ShieldedInput> inputShielded;
            txptr->GetParameter(TxParameterID::InputCoinsShielded, inputShielded);
            if (std::find(inputShielded.begin(), inputShielded.end(), shieldedEvt.m_CoinID) != inputShielded.end())
            {
                shieldedCoin->m_Status = ShieldedCoin::Status::Outgoing;;
                shieldedCoin->m_spentTxId = txid;
                BEAM_LOG_INFO() << "Shielded output, ID: " << shieldedEvt.m_TxoID << " marked as Outgoing";
            }
        }

        m_WalletDB->saveShieldedCoin(*shieldedCoin);

        BEAM_LOG_INFO() << "Shielded output, ID: " << shieldedEvt.m_TxoID << (isAdd ? " Confirmed" : " Spent") << ", Height=" << h;
        RestoreTransactionFromShieldedCoin(*shieldedCoin);
    }

    void Wallet::OnRolledBack()
    {
        Block::SystemState::Full sTip;
        m_WalletDB->get_History().get_Tip(sTip);

        Block::SystemState::ID id;
        sTip.get_ID(id);
        BEAM_LOG_INFO() << "Rolled back to " << id;

        m_WalletDB->setSystemStateID(id);
        m_WalletDB->get_History().DeleteFrom(sTip.m_Height + 1);
        m_WalletDB->rollbackConfirmedUtxo(sTip.m_Height);
        m_WalletDB->rollbackConfirmedShieldedUtxo(sTip.m_Height);
        m_WalletDB->rollbackAssets(sTip.m_Height);
        m_WalletDB->deleteEventsFrom(sTip.m_Height + 1);

        // Rollback active transaction
        for (auto it = m_ActiveTransactions.begin(); m_ActiveTransactions.end() != it; it++)
        {
            const auto& pTx = it->second;
            if (pTx->Rollback(sTip.m_Height))
            {
                UpdateOnSynced(pTx);
            }
        }

        // Rollback inactive (completed or active) transactions if applicable
        auto txs = m_WalletDB->getTxHistory(TxType::ALL); // get list of ALL transactions
        for (auto& tx : txs)
        {
            // For all transactions that are not currently in the 'active' tx list
            if (m_ActiveTransactions.find(tx.m_txId) == m_ActiveTransactions.end())
            {
                // Reconstruct tx with reset parameters and add it to the active list
                auto pTx = ConstructTransaction(tx.m_txId, tx.m_txType);
                if (pTx && pTx->Rollback(sTip.m_Height))
                {
                    m_ActiveTransactions.emplace(tx.m_txId, pTx);
                    UpdateOnSynced(pTx);
                }
            }
        }

        Height h = GetEventsHeightNext();
        if (h > sTip.m_Height + 1)
        {
            SetEventsHeight(sTip.m_Height);
        }
    }

    void Wallet::OnEventsSerif(const Hash::Value& hv, Height h)
    {
        static const char szEvtSerif[] = "EventsSerif";

        HeightHash hh;
        if (!storage::getVar(*m_WalletDB, szEvtSerif, hh))
        {
            hh.m_Hash = Zero;
            // If this is the 1st time we received the serif - we do NOT assume the wallet was synced ok. It can potentially be already out-of-sync.
            // Hence once node and wallet are both upgraded from older version - there will always be initial rescan.
            hh.m_Height = MaxHeight;
        }

        bool bHashChanged = (hh.m_Hash != hv);
        bool bMustRescan = bHashChanged;
        if (bHashChanged)
        {
            // Node Serif has changed (either connected to different node, or it rescanned the blockchain). The events stream may not be consistent with ours.
            Height h0 = GetEventsHeightNext();
            if (!h0)
                bMustRescan = false; // nothing to rescan atm
            else
            {
                if (h0 > hh.m_Height)
                {
                    // Our events are consistent and full up to height h0-1.
                    bMustRescan = (h >= h0);
                }
            }

            hh.m_Hash = hv;
        }

        if (bHashChanged || (h != hh.m_Height))
        {
            BEAM_LOG_INFO() << "Events Serif changed: " << (bHashChanged ? "new Hash, " : "") << "Height=" << h << (bMustRescan ? ", Resyncing" : "");

            hh.m_Height = h;
            storage::setVar(*m_WalletDB, szEvtSerif, hh);
        }

        if (bMustRescan)
            Rescan();

    }

    void Wallet::OnDependentStateChanged()
    {
        uint32_t nCount = 0;
        const auto* pHv = get_DependentState(nCount);

        std::ostringstream os;
        os << "HFT state changed";

        for (uint32_t i = 0; i < nCount; i++)
            os << "\n\t" << pHv[i];

        BEAM_LOG_INFO() << os.str();

        for (auto it = m_ActiveTransactions.begin(); m_ActiveTransactions.end() != it; it++)
            it->second->OnDependentStateChanged();
    }

    void Wallet::HftSubscribe(bool bSubscribe)
    {
        if (bSubscribe)
        {
            if (!m_HftSubscribed)
                m_NodeEndpoint->DependentSubscribe(true);

            m_HftSubscribed++;
        }
        else
        {
            if (m_HftSubscribed)
            {
                --m_HftSubscribed;
                if (!m_HftSubscribed)
                    m_NodeEndpoint->DependentSubscribe(false);
            }
        }
    }

    const Merkle::Hash* Wallet::get_DependentState(uint32_t& nCount)
    {
        return m_NodeEndpoint->get_DependentState(nCount);
    }

    void Wallet::OnNewPeer(const PeerID& id, io::Address address)
    {
        constexpr size_t MaxPeers = 10;
        std::deque<io::Address> addresses;
        storage::getBlobVar(*m_WalletDB, FallbackPeers, addresses);
        addresses.push_back(address);
        if (addresses.size() > MaxPeers)
        {
            addresses.pop_front();
        }
        storage::setBlobVar(*m_WalletDB, FallbackPeers, addresses);
    }

    void Wallet::OnNewTip()
    {
        m_WalletDB->ShrinkHistory();

        Block::SystemState::Full sTip;
        get_tip(sTip);
        if (!sTip.m_Height)
            return; //?!

        Block::SystemState::ID id;
        sTip.get_ID(id);
        BEAM_LOG_INFO() << "Sync up to " << id;

        if (!SyncRemains())
        {
            m_Extra.m_ShieldedOutputs = m_WalletDB->get_ShieldedOuts();
        }

        RequestBodies();
        RequestEvents();
        RequestStateSummary();

        for (auto& tx : m_NextTipTransactionToUpdate)
        {
            UpdateOnSynced(tx);
        }
        m_NextTipTransactionToUpdate.clear();

        CheckSyncDone();

        ProcessStoredMessages();
    }

    void Wallet::OnTipUnchanged()
    {
        BEAM_LOG_INFO() << "Tip has not been changed";

        RequestBodies();

        CheckSyncDone();

        ProcessStoredMessages();
    }

    void Wallet::getUtxoProof(const Coin& coin)
    {
        MyRequestUtxo::Ptr pReq(new MyRequestUtxo);
        pReq->m_CoinID = coin.m_ID;

        if (!m_WalletDB->get_CommitmentSafe(pReq->m_Msg.m_Utxo, coin.m_ID))
        {
            BEAM_LOG_WARNING() << "You cannot get utxo commitment without private key";
            return;
        }

        LOG_DEBUG() << "Get utxo proof: " << pReq->m_Msg.m_Utxo;

        PostReqUnique(*pReq);
    }

    size_t Wallet::SyncRemains() const
    {
        size_t val =
#define THE_MACRO(type) m_Pending##type.size() +
            REQUEST_TYPES_Sync(THE_MACRO)
#undef THE_MACRO
            0;

        return val;
    }

    size_t Wallet::GetSyncDone() const
    {
        auto val = SyncRemains();
        assert(val <= m_LastSyncTotal);
        size_t done = m_LastSyncTotal - val;
        done += m_BlocksDone;
        return done;
    }

    size_t Wallet::GetSyncTotal() const
    {
        return m_LastSyncTotal + m_RequestedBlocks;
    }

    void Wallet::CheckSyncDone()
    {
        ReportSyncProgress();

        if (SyncRemains())
            return;

        m_LastSyncTotal = 0;
        m_RequestedBlocks = 0;
        m_BlocksDone = 0;

        SaveKnownState();
    }

    void Wallet::SaveKnownState()
    {
        Block::SystemState::Full sTip;
        get_tip(sTip);

        Block::SystemState::ID id;
        if (sTip.m_Height)
            sTip.get_ID(id);
        else
            ZeroObject(id);

        m_WalletDB->setSystemStateID(id);
        BEAM_LOG_INFO() << "Current state is " << id;
        NotifySyncProgress();

        if (!IsValidTimeStamp(sTip.m_TimeStamp))
        {
            // we are not ready to process transactions
            return;
        }

        ActionQueue actionQueue;
        actionQueue.swap(m_SyncActionsQueue);

        if (!actionQueue.empty())
        {
            AsyncContextHolder async(*this);
            while (!actionQueue.empty())
            {
                auto& action = actionQueue.front();
                action();
                actionQueue.pop();
            }
        }

        std::set<beam::Asset::ID> assets;
        m_WalletDB->visitCoins([&assets] (const Coin& c) -> bool
        {
            if (c.m_ID.m_AssetID)
            {
                assets.insert(c.m_ID.m_AssetID);
            }
            return true;
        });

        m_WalletDB->visitAssets([&assets] (const WalletAsset& asset) -> bool
        {
           assets.erase(asset.m_ID);
           return true;
        });

        for (auto assetId: assets)
        {
            confirm_asset(assetId);
        }

        LOG_DEBUG() << TRACE(IsMobileNodeEnabled()) << TRACE(m_Extra.m_ShieldedOutputs) << " Node shielded outs=" << m_WalletDB->get_ShieldedOuts();
        assert(m_Extra.m_ShieldedOutputs == m_WalletDB->get_ShieldedOuts());
        storage::setNeedToRequestBodies(*m_WalletDB, false); // disable body requests after importing recovery or rescan
    }

    void Wallet::NotifySyncProgress()
    {
        auto total = GetSyncTotal();
        auto done = GetSyncDone();
        assert(done <= total);
        for (const auto sub : m_subscribers)
        {
            sub->onSyncProgress(static_cast<int>(done), static_cast<int>(total));
        }
    }

    void Wallet::ReportSyncProgress()
    {
        if (!m_LastSyncTotal)
            return;
        auto total = GetSyncTotal();
        auto done = GetSyncDone();
        assert(done <= total);
        int p = static_cast<int>((done * 100) / total);
        BEAM_LOG_INFO() << "Synchronizing with node: " << p << "% (" << done << "/" << total << ")";

        NotifySyncProgress();
    }

    void Wallet::SendTransactionToNode(const TxID& txId, const Transaction::Ptr& data, const Merkle::Hash* pParentCtx, SubTxID subTxID)
    {
        LOG_DEBUG() << txId << "[" << subTxID << "]" << " sending tx for registration";

#ifndef NDEBUG
        TxBase::Context ctx;
        ctx.m_Height.m_Min = m_WalletDB->getCurrentHeight();
        assert(data->IsValid(ctx));
#endif // NDEBUG

        MyRequestTransaction::Ptr pReq(new MyRequestTransaction);
        pReq->m_TxID = txId;
        pReq->m_SubTxID = subTxID;
        pReq->m_Msg.m_Transaction = data;

        if (pParentCtx)
        {
            pReq->m_Msg.m_Context = std::make_unique<Merkle::Hash>(*pParentCtx);
            pReq->m_Msg.m_Fluff = true;
        }

        PostReqUnique(*pReq);
    }

    void Wallet::register_tx(const TxID& txId, const Transaction::Ptr& data, const Merkle::Hash* pParentCtx, SubTxID subTxID)
    {
        SendTransactionToNode(txId, data, pParentCtx, subTxID);
    }

    void Wallet::Subscribe(IWalletObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());
        m_subscribers.push_back(observer);
        m_WalletDB->Subscribe(observer);
    }

    void Wallet::Unsubscribe(IWalletObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);
        assert(it != m_subscribers.end());
        m_subscribers.erase(it);
        m_WalletDB->Unsubscribe(observer);
    }

    void Wallet::Subscribe(ISimpleSwapHandler* handler)
    {
        if (m_ssHandler)
        {
            assert(false);
            throw std::runtime_error("Can be only one SimpleSwap handler");
        }
        m_ssHandler = handler;
    }

    void Wallet::Unsubscribe(ISimpleSwapHandler* handler)
    {
        if (m_ssHandler != handler)
        {
            assert(false);
            throw std::runtime_error("Unexpected SimpleSwap unsubscribe");
        }
        m_ssHandler = nullptr;
    }

    void Wallet::OnTransactionMsg(const WalletID& myID, const SetTxParameter& msg)
    {
        //
        // Extend Auto addresses
        //
        if (auto addr = m_WalletDB->getAddress(myID); addr)
        {
            if (!addr->isPermanent())
            {
                addr->setExpirationStatus(WalletAddress::ExpirationStatus::Auto);
                m_WalletDB->saveAddress(*addr);
            }
        }

        //
        // Process transaction request
        //
        auto it = m_ActiveTransactions.find(msg.m_TxID);
        if (it != m_ActiveTransactions.end())
        {
            const auto& pTx = it->second;

            if (pTx->GetType() != msg.m_Type)
            {
                BEAM_LOG_WARNING() << msg.m_TxID << " Parameters for invalid tx type";
                return;
            }

            WalletID peerID;
            if (pTx->GetParameter(TxParameterID::PeerAddr, peerID))
            {
                if (peerID != msg.m_From)
                    return; // if we already have PeerID, we should ignore messages from others
            }
            else
            {
                pTx->SetParameter(TxParameterID::PeerAddr, msg.m_From, false);
            }

            if (ApplyTransactionParameters(pTx, msg.m_Parameters, false))
                UpdateTransaction(pTx);

            return;
        }

        {
            TxType type = TxType::Simple;
            if (storage::getTxParameter(*m_WalletDB, msg.m_TxID, TxParameterID::TransactionType, type)) {
                // This request has already been processed
                // assert should always be OK, if triggered means that we have mistake in the code OR
                // somebody is trying to tamper with our transactions
                assert(type == msg.m_Type);
                return;
            }
        }

        if (msg.m_Type == TxType::AtomicSwap)
        {
            // we don't create swap from SBBS message
            return;
        }

        bool isSender = false;
        if (!msg.GetParameter(TxParameterID::IsSender, isSender))
        {
            // we don't accept txs without IsSender
            return;
        }

        if (isSender)
        {
            // TxParameterID::IsSender should always be false if it is coming from outside
            assert(false);
            return;
        }

        if (msg.m_Type == TxType::DexSimpleSwap)
        {
            if (m_ssHandler == nullptr)
            {
                BEAM_LOG_WARNING() << "DexSimpleSwap tx is received but feature is disabled. " << "TxID is " << msg.m_TxID;
                return;
            }
        }

        if (msg.m_Type == TxType::Simple || msg.m_Type == TxType::DexSimpleSwap)
        {
            if (msg.m_Type == TxType::DexSimpleSwap)
            {
                if (!m_ssHandler->acceptIncomingDexSS(msg))
                {
                    // TODO:DEX create tx and fail tx with rejected reason to make the peer not wait
                    BEAM_LOG_INFO() << "Incoming DexSimpleSwap rejected. "
                               << "DexOrderID [" << msg.GetParameterOrDefault<DexOrderID>(TxParameterID::ExternalDexOrderID) << "] "
                               << "TxID " << msg.m_TxID;
                    return;
                }
            }

            auto pTx = ConstructTransaction(msg.m_TxID, msg.m_Type);
            if (!pTx)
            {
                return;
            }

            pTx->SetParameter(TxParameterID::TransactionType, msg.m_Type, true);
            pTx->SetParameter(TxParameterID::CreateTime, getTimestamp(),true);
            pTx->SetParameter(TxParameterID::MyAddr, myID, false);
            pTx->SetParameter(TxParameterID::PeerAddr, msg.m_From, false);
            pTx->SetParameter(TxParameterID::IsInitiator, false, false);
            pTx->SetParameter(TxParameterID::Status, TxStatus::Pending, true);

            auto address = m_WalletDB->getAddress(myID);
            if (address.is_initialized())
            {
                ByteBuffer message(address->m_label.begin(), address->m_label.end());
                pTx->SetParameter(TxParameterID::Message, message);
            }

            MakeTransactionActive(pTx);
            ApplyTransactionParameters(pTx, msg.m_Parameters, false);
            UpdateTransaction(pTx);

            if (msg.m_Type == TxType::DexSimpleSwap)
            {
                m_ssHandler->onDexTxCreated(msg, pTx);
                BEAM_LOG_INFO() << "Incoming DexSimpleSwap accepted. TxID is " << msg.m_TxID;
            }
        }
        else
        {
            assert(false);
            BEAM_LOG_WARNING() << "Unsupported TX Type requested via SBBS: "
                          << " type: " << static_cast<unsigned int>(msg.m_Type)
                          << " txid: " << msg.m_TxID;
            return;
        }
    }

    BaseTransaction::Ptr Wallet::ConstructTransaction(const TxID& id, TxType type)
    {
        auto it = m_TxCreators.find(type);
        if (it == m_TxCreators.end())
        {
            BEAM_LOG_WARNING() << id << " Unsupported type of transaction: " << static_cast<int>(type);
            return wallet::BaseTransaction::Ptr();
        }

        return it->second->Create(BaseTransaction::TxContext(*this, *this, id));
    }

    BaseTransaction::Ptr Wallet::ConstructTransactionFromParameters(const TxParameters& parameters)
    {
        auto type = parameters.GetParameter<TxType>(TxParameterID::TransactionType);
        if (!type)
        {
            return BaseTransaction::Ptr();
        }

        auto it = m_TxCreators.find(*type);
        if (it == m_TxCreators.end())
        {
            BEAM_LOG_ERROR() << *parameters.GetTxID() << " Unsupported type of transaction: " << static_cast<int>(*type);
            return BaseTransaction::Ptr();
        }

        auto completedParameters = it->second->CheckAndCompleteParameters(parameters);

        auto newTx = it->second->Create(BaseTransaction::TxContext(*this, *this, *parameters.GetTxID()));
        ApplyTransactionParameters(newTx, completedParameters.Pack(), true);
        return newTx;
    }

    void Wallet::MakeTransactionActive(BaseTransaction::Ptr tx)
    {
        m_ActiveTransactions.emplace(tx->GetTxID(), tx);
    }

    void Wallet::ProcessStoredMessages()
    {
        if (m_MessageEndpoints.empty() || m_StoredMessagesProcessed)
        {
            return;
        }
        auto messages = m_WalletDB->getWalletMessages();
        for (auto& message : messages)
        {
            for (auto& endpoint : m_MessageEndpoints)
            {
                endpoint->SendRawMessage(message.m_PeerID, message.m_Message);
            }
            m_WalletDB->deleteWalletMessage(message.m_ID);
        }
        m_StoredMessagesProcessed = true;
    }

    bool Wallet::IsNodeInSync() const
    {
        if (m_NodeEndpoint)
        {
            return IsWalletInSync();
        }
        return true; // to allow made air-gapped transactions
    }

    void Wallet::RequestStateSummary()
    {
        MyRequestStateSummary::Ptr pReq(new MyRequestStateSummary);
        PostReqUnique(*pReq);
    }

    void Wallet::RequestShieldedOutputsAt(Height h, std::function<void(Height, TxoID)>&& onRequestComplete)
    {
        MyRequestShieldedOutputsAt::Ptr pVal(new MyRequestShieldedOutputsAt);
        pVal->m_Msg.m_Height = h;
        pVal->m_callback = std::move(onRequestComplete);
        PostReqUnique(*pVal);
    }

    void Wallet::RequestAssetsListAt(Height h, std::function<void(std::vector<beam::Asset::Full>&&)>&& onRequestComplete)
    {
        if (!m_PendingAssetsListAt.empty())
        {
            DeleteReq(*m_PendingAssetsListAt.begin());
        }
        MyRequestAssetsListAt::Ptr pVal(new MyRequestAssetsListAt);
        pVal->m_Msg.m_Height = h;
        pVal->m_callback = std::move(onRequestComplete);
        PostReqUnique(*pVal);
    }

    bool Wallet::IsConnectedToOwnNode() const
    {
        return m_OwnedNodesOnline > 0;
    }

    bool Wallet::CanDetectCoins() const
    {
        return m_OwnedNodesOnline || m_IsBodyRequestsEnabled;
    }

    void Wallet::EnableBodyRequests(bool value)
    {
        m_IsBodyRequestsEnabled = value;
    }

    void Wallet::RestoreTransactionFromShieldedCoin(ShieldedCoin& coin)
    {
        // add virtual transaction for receiver
        storage::restoreTransactionFromShieldedCoin(*m_WalletDB, coin, *this);
    }

    void Wallet::SetTreasuryHandled(bool value)
    {
        m_IsTreasuryHandled = value;
        storage::setTreasuryHandled(*m_WalletDB, value);
    }

    void Wallet::CacheCommitments()
    {
        if (m_IsCommitmentsCached)
            return;

        m_WalletDB->visitCoins([&](const Coin& c)
        {
            if (c.m_status != Coin::Status::Available &&
                c.m_status != Coin::Status::Outgoing && 
                c.m_status != Coin::Status::Maturing)
                return true;

            ECC::Point comm;
            if (m_WalletDB->get_CommitmentSafe(comm, c.m_ID))
            {
                m_Commitments.emplace(comm, c.m_maturity);
            }
            if (c.m_ID.IsBb21Possible())
            {
                CoinID cid = c.m_ID;
                cid.set_WorkaroundBb21();
                if (m_WalletDB->get_CommitmentSafe(comm, cid))
                {
                    m_Commitments.emplace(comm, c.m_maturity);
                }
            }
            return true;
        });
        m_IsCommitmentsCached = true;
    }

    void Wallet::CacheCommitment(const ECC::Point& comm, Height maturity, bool add)
    {
        if (!IsMobileNodeEnabled())
            return;

        if (add)
        {
            m_Commitments.emplace(comm, maturity);
        }
        else
        {
            m_Commitments.erase(comm);
        }
    }

    void Wallet::ResetCommitmentsCache()
    {
        m_Commitments.clear();
        m_IsCommitmentsCached = false;
    }

    bool Wallet::IsMobileNodeEnabled() const
    {
        return !m_OwnedNodesOnline
            && (m_IsBodyRequestsEnabled || storage::needToRequestBodies(*m_WalletDB))
            && !!m_WalletDB->get_MasterKdf();
    }

    void Wallet::assertThread() const
    {
        if (_myThread != std::this_thread::get_id())
        {
            assert(false);
            throw std::runtime_error("Wallet accessed from wrong thread");
        }
    }

    void Wallet::markAppNotificationAsRead(const TxID& id)
    {
        auto it = m_ActiveTransactions.find(id);
        if (m_ActiveTransactions.end() == it)
        {
            return;
        }

        if (!storage::setTxParameter(*m_WalletDB, id, TxParameterID::IsContractNotificationMarkedAsRead, true, true))
            BEAM_LOG_ERROR() << "Can't mark application notification as read.";
    }

    void Wallet::sendInstantSbbsMessage(beam::Timestamp timestamp, const WalletID& peerID, const WalletID& myID, ByteBuffer&& message)
    {
        SetTxParameter msg;
        msg.m_From = myID;
        msg.m_Type = TxType::InstantSbbsMessage;

        msg.AddParameter((TxParameterID) 0, message.size());
        msg.AddParameter((TxParameterID) 1, timestamp);
        msg.AddParameter((TxParameterID) 2, message);

        SendSpecialMsg(peerID, msg);
    }
}
