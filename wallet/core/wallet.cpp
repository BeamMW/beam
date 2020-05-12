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
#include "utility/logger.h"
#include "utility/helpers.h"
#include "simple_transaction.h"
#include "strings_resources.h"
#include "assets_utils.h"

#include <algorithm>
#include <random>
#include <iomanip>
#include <numeric>

namespace beam::wallet
{
    using namespace std;
    using namespace ECC;

    namespace
    {
        bool ApplyTransactionParameters(BaseTransaction::Ptr tx, const PackedTxParameters& parameters, bool isInternalSource, bool allowPrivate = false)
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

                if (allowPrivate || p.first < TxParameterID::PrivateFirstParam)
                {
                    if (!isInternalSource && !tx->IsTxParameterExternalSettable(p.first, subTxID))
                    {
                        LOG_WARNING() << tx->GetTxID() << "Attempt to set internal tx parameter: " << static_cast<int>(p.first);
                        continue;
                    }
                    txChanged |= tx->SetParameter(p.first, p.second, subTxID);
                }
                else
                {
                    LOG_WARNING() << "Attempt to set private tx parameter";
                }
            }
            return txChanged;
        }
    }

    // @param SBBS address as string
    // Returns whether the address is a valid SBBS address i.e. a point on an ellyptic curve
    bool check_receiver_address(const std::string& addr)
    {
        WalletID walletID;
        return
            walletID.FromHex(addr) &&
            walletID.IsValid();
    }

    const char Wallet::s_szNextEvt[] = "NextUtxoEvent"; // any event, not just UTXO. The name is for historical reasons

    Wallet::Wallet(IWalletDB::Ptr walletDB, TxCompletedAction&& action, UpdateCompletedAction&& updateCompleted)
        : m_WalletDB{ walletDB }
        , m_TxCompletedAction{ move(action) }
        , m_UpdateCompleted{ move(updateCompleted) }
        , m_LastSyncTotal(0)
        , m_OwnedNodesOnline(0)
    {
        assert(walletDB);
        // the only default type of transaction
        RegisterTransactionType(TxType::Simple, make_unique<SimpleTransaction::Creator>(m_WalletDB));
    }

    Wallet::~Wallet()
    {
        CleanupNetwork();
    }

    void Wallet::CleanupNetwork()
    {
        // clear all requests
#define THE_MACRO(type, msgOut, msgIn) \
                while (!m_Pending##type.empty()) \
                    DeleteReq(*m_Pending##type.begin());

        REQUEST_TYPES_All(THE_MACRO)
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
                RequestEvents(); // maybe time to refresh UTXOs
        }
        else
        {
            assert(m_OwnedNodesOnline); // check that m_OwnedNodesOnline is positive number
            if (!--m_OwnedNodesOnline)
                AbortEvents();
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

    void Wallet::SetNodeEndpoint(std::shared_ptr<proto::FlyClient::INetwork> nodeEndpoint)
    {
        m_NodeEndpoint = std::move(nodeEndpoint);
    }

    void Wallet::AddMessageEndpoint(IWalletMessageEndpoint::Ptr endpoint)
    {
        m_MessageEndpoints.insert(endpoint);
    }

    // Rescan the blockchain for UTXOs
    void Wallet::Rescan()
    {
        // We save all Incoming coins of active transactions and
        // restore them after clearing db. This will save our outgoing & available amounts
        std::vector<Coin> ocoins;
        for (const auto& tx : m_ActiveTransactions)
        {
            const auto& txocoins = m_WalletDB->getCoinsCreatedByTx(tx.first);
            ocoins.insert(ocoins.end(), txocoins.begin(), txocoins.end());
        }

        m_WalletDB->clearCoins();

        // Restore Incoming coins of active transactions
        m_WalletDB->saveCoins(ocoins);

        storage::setVar(*m_WalletDB, s_szNextEvt, 0);
        RequestEvents();
    }

    void Wallet::RegisterTransactionType(TxType type, BaseTransaction::Creator::Ptr creator)
    {
        m_TxCreators[type] = move(creator);
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

    void Wallet::ResumeAllTransactions()
    {
        auto txs = m_WalletDB->getTxHistory(TxType::ALL);
        for (auto& tx : txs)
        {
            ResumeTransaction(tx);
        }
    }

    bool Wallet::IsWalletInSync() const
    {
        Block::SystemState::Full state;
        get_tip(state);

        return IsValidTimeStamp(state.m_TimeStamp);
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
            LOG_DEBUG() << "Async update started!";
        }
        ++m_AsyncUpdateCounter;
    }

    void Wallet::OnAsyncFinished()
    {
        if (--m_AsyncUpdateCounter == 0)
        {
            LOG_DEBUG() << "Async update finished!";
            if (m_UpdateCompleted)
            {
                m_UpdateCompleted();
            }
        }
    }

    void Wallet::on_tx_completed(const TxID& txID)
    {
        // Note: the passed TxID is (most probably) the member of the transaction, 
        // which we, most probably, are going to erase from the map, which can potentially delete it.
        // Make sure we either copy the txID, or prolong the lifetime of the tx.

        BaseTransaction::Ptr pGuard;

        auto it = m_ActiveTransactions.find(txID);
        if (it != m_ActiveTransactions.end())
        {
            pGuard.swap(it->second);
            m_ActiveTransactions.erase(it);
            pGuard->FreeResources();
        }

        if (m_TxCompletedAction)
        {
            m_TxCompletedAction(txID);
        }
    }

    // Implementation of the INegotiatorGateway::confirm_outputs
    // TODO: Not used anywhere, consider removing
    void Wallet::confirm_outputs(const vector<Coin>& coins)
    {
        for (auto& coin : coins)
            getUtxoProof(coin);
    }

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
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestTransaction::operator < (const MyRequestTransaction& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestEvents::operator < (const MyRequestEvents& x) const
    {
        return false;
    }

    bool Wallet::MyRequestProofShieldedOutp::operator < (const MyRequestProofShieldedOutp& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestShieldedList::operator < (const MyRequestShieldedList& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestStateSummary::operator < (const MyRequestStateSummary& x) const
    {
        return false;
    }

    void Wallet::RequestHandler::OnComplete(Request& r)
    {
        uint32_t n = get_ParentObj().SyncRemains();

        switch (r.get_Type())
        {
#define THE_MACRO(type, msgOut, msgIn) \
        case Request::Type::type: \
            { \
                MyRequest##type& x = static_cast<MyRequest##type&>(r); \
                get_ParentObj().DeleteReq(x); \
                get_ParentObj().OnRequestComplete(x); \
            } \
            break;

            REQUEST_TYPES_All(THE_MACRO)
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
            MyRequestKernel::Ptr pVal(new MyRequestKernel);
            pVal->m_TxID = txID;
            pVal->m_SubTxID = subTxID;
            pVal->m_Msg.m_ID = kernelID;

            if (PostReqUnique(*pVal))
                LOG_INFO() << txID << "[" << subTxID << "]" << " Get proof for kernel: " << pVal->m_Msg.m_ID;
        }
    }

    void Wallet::confirm_asset(const TxID& txID, const PeerID& ownerID, SubTxID subTxID)
    {
        if (auto it = m_ActiveTransactions.find(txID); it != m_ActiveTransactions.end())
        {
            MyRequestAsset::Ptr pVal(new MyRequestAsset);
            pVal->m_TxID = txID;
            pVal->m_SubTxID = subTxID;
            pVal->m_Msg.m_Owner = ownerID;
            pVal->m_Msg.m_AssetID = Asset::s_InvalidID;

            if (PostReqUnique(*pVal))
                LOG_INFO() << txID << "[" << subTxID << "]" << " Get proof for asset with the owner ID: " << ownerID;
        }
    }

    void Wallet::confirm_asset(const TxID& txID, Asset::ID assetId, SubTxID subTxID)
    {
        if (auto it = m_ActiveTransactions.find(txID); it != m_ActiveTransactions.end())
        {
            MyRequestAsset::Ptr pVal(new MyRequestAsset);
            pVal->m_TxID = txID;
            pVal->m_SubTxID = subTxID;
            pVal->m_Msg.m_Owner = Asset::s_InvalidOwnerID;
            pVal->m_Msg.m_AssetID = assetId;

            if (PostReqUnique(*pVal))
                LOG_INFO() << txID << "[" << subTxID << "]" << " Get proof for asset with id: " << assetId;
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
                LOG_INFO() << txID << "[" << subTxID << "]" << " Get details for kernel: " << msg.m_ID;
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
        pVal->m_callback = callback;
        pVal->m_TxID = txId;

        pVal->m_Msg.m_Id0 = startIndex;
        pVal->m_Msg.m_Count = count;

        if (PostReqUnique(*pVal))
        {
            LOG_INFO() << txId << " Get shielded list, start_index = " << startIndex << ", count = " << count;
        }
    }

    void Wallet::get_proof_shielded_output(const TxID& txId, ECC::Point serialPublic, ProofShildedOutputCallback&& callback)
    {
        MyRequestProofShieldedOutp::Ptr pVal(new MyRequestProofShieldedOutp);
        pVal->m_callback = callback;
        pVal->m_TxID = txId;

        pVal->m_Msg.m_SerialPub = serialPublic;

        if (PostReqUnique(*pVal))
        {
            LOG_INFO() << txId << " Get proof of shielded output.";
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

    void Wallet::OnWalletMessage(const WalletID& myID, const SetTxParameter& msg)
    {
        auto t = GetTransaction(myID, msg);
        if (!t)
        {
            return;
        }

        if (ApplyTransactionParameters(t, msg.m_Parameters, false))
        {
            UpdateTransaction(msg.m_TxID);
        }
    }

    void Wallet::OnRequestComplete(MyRequestTransaction& r)
    {
        LOG_DEBUG() << r.m_TxID << "[" << r.m_SubTxID << "]" << " register status " << static_cast<uint32_t>(r.m_Res.m_Value);

        auto it = m_ActiveTransactions.find(r.m_TxID);
        if (it != m_ActiveTransactions.end())
        {
            it->second->SetParameter(TxParameterID::TransactionRegistered, r.m_Res.m_Value, r.m_SubTxID);
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
        LOG_INFO() << txId << " Canceling tx";

        if (auto it = m_ActiveTransactions.find(txId); it != m_ActiveTransactions.end())
        {
            it->second->Cancel();
        }
        else
        {
            LOG_WARNING() << "Transaction already inactive";
        }
    }

    void Wallet::DeleteTransaction(const TxID& txId)
    {
        LOG_INFO() << "deleting tx " << txId;
        if (auto it = m_ActiveTransactions.find(txId); it == m_ActiveTransactions.end())
        {
            m_WalletDB->deleteTx(txId);
        }
        else
        {
            LOG_WARNING() << "Cannot delete running transaction";
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

    void Wallet::UpdateTransaction(BaseTransaction::Ptr tx)
    {
        bool bSynced = !SyncRemains() && IsNodeInSync();

        if (bSynced)
        {
            AsyncContextHolder holder(*this);
            tx->Update();
        }
        else
        {
            UpdateOnSynced(tx);
        }
    }

    void Wallet::UpdateOnSynced(BaseTransaction::Ptr tx)
    {
        m_TransactionsToUpdate.insert(tx);
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
        // we don't know the real height, but it'll be used for logging only. For standard outputs maturity and height are the same
        ProcessEventUtxo(r.m_CoinID, proof.m_State.m_Maturity, proof.m_State.m_Maturity, true);
    }

    void Wallet::OnRequestComplete(MyRequestKernel& r)
    {
        auto it = m_ActiveTransactions.find(r.m_TxID);
        if (m_ActiveTransactions.end() == it)
        {
            return;
        }
        auto tx = it->second;
        if (!r.m_Res.m_Proof.empty())
        {
            m_WalletDB->get_History().AddStates(&r.m_Res.m_Proof.m_State, 1); // why not?

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
            UpdateOnNextTip(tx);
        }
    }

    void Wallet::OnRequestComplete(MyRequestAsset& req)
    {
        const auto it = m_ActiveTransactions.find(req.m_TxID);
        if (m_ActiveTransactions.end() == it)
        {
            return;
        }

        Block::SystemState::Full sTip;
        get_tip(sTip);
        auto tx = it->second;

        if (!req.m_Res.m_Proof.empty())
        {
            const auto& info  = req.m_Res.m_Info;
            const auto height = m_WalletDB->getCurrentHeight();
            m_WalletDB->saveAsset(info, height);

            tx->SetParameter(TxParameterID::AssetConfirmedHeight, height, req.m_SubTxID);
            tx->SetParameter(TxParameterID::AssetInfoFull, info, req.m_SubTxID);
            tx->SetParameter(TxParameterID::AssetUnconfirmedHeight, Height(0), req.m_SubTxID);

            LOG_INFO() << req.m_TxID << "[" << req.m_SubTxID << "]" << " Received proof for Asset with ID " << info.m_ID;
            LOG_INFO() << req.m_TxID << "[" << req.m_SubTxID << "]" << " Asset ID: "           << info.m_ID;
            LOG_INFO() << req.m_TxID << "[" << req.m_SubTxID << "]" << " Owner ID: "           << info.m_Owner;
            LOG_INFO() << req.m_TxID << "[" << req.m_SubTxID << "]" << " Issued amount: "      << PrintableAmount(info.m_Value, false, kAmountASSET, kAmountAGROTH);
            LOG_INFO() << req.m_TxID << "[" << req.m_SubTxID << "]" << " Lock Height: "        << info.m_LockHeight;
            LOG_INFO() << req.m_TxID << "[" << req.m_SubTxID << "]" << " Metadata size: "      << info.m_Metadata.m_Value.size() << " bytes";
            LOG_INFO() << req.m_TxID << "[" << req.m_SubTxID << "]" << " Refresh height: "     << height;

            const WalletAssetMeta meta(info);
            meta.LogInfo();

            if (tx->GetType() == TxType::AssetReg)
            {
                m_WalletDB->markAssetOwned(info.m_ID);
            }

            if(const auto wasset = m_WalletDB->findAsset(info.m_ID))
            {
                if(wasset->m_IsOwned)
                {
                    LOG_INFO() << req.m_TxID << "[" << req.m_SubTxID << "]" << " You own this asset";
                }
            }

            UpdateTransaction(tx);
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

            tx->SetParameter(TxParameterID::AssetConfirmedHeight, Height(0), req.m_SubTxID);
            tx->SetParameter(TxParameterID::AssetInfoFull, Asset::Full(), req.m_SubTxID);
            tx->SetParameter(TxParameterID::AssetUnconfirmedHeight, sTip.m_Height, req.m_SubTxID);
            UpdateTransaction(tx);
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

    void Wallet::OnRequestComplete(MyRequestProofShieldedInp& r)
    {
        // TODO(alex.starun): implement this
    }

    void Wallet::OnRequestComplete(MyRequestProofShieldedOutp& r)
    {
        if (!r.m_Res.m_Proof.empty())
        {
            r.m_callback(r.m_Res);
        }
    }

    void Wallet::OnRequestComplete(MyRequestBbsMsg& r)
    {
        assert(false);
    }

    void Wallet::OnRequestComplete(MyRequestStateSummary& r)
    {
        // TODO: save full response?
        storage::setVar(*m_WalletDB, kStateSummaryShieldedOutsDBPath, r.m_Res.m_ShieldedOuts);
    }

    void Wallet::RequestEvents()
    {
        if (!m_OwnedNodesOnline)
            return;

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

            virtual void OnEvent(proto::Event::Base& evt_) override
            {
                switch (evt_.get_Type())
                {
                    case proto::Event::Type::Shielded:
                    {
                        proto::Event::Shielded& shieldedEvt = Cast::Up<proto::Event::Shielded>(evt_);
                        m_This.ProcessEventShieldedUtxo(shieldedEvt, m_Height);
                        return;
                    }
                    case proto::Event::Type::Utxo:
                    {
                        proto::Event::Utxo& evt = Cast::Up<proto::Event::Utxo>(evt_);

                        // filter-out false positives

                        if (!m_This.m_WalletDB->IsRecoveredMatch(evt.m_Cid, evt.m_Commitment))
                            return;

                        bool bAdd = 0 != (proto::Event::Flags::Add & evt.m_Flags);
                        m_This.ProcessEventUtxo(evt.m_Cid, m_Height, evt.m_Maturity, bAdd);
                        return;
                    }
                    default:
                        break;
                }
            }
        } p(*this);
        
        uint32_t nCount = p.Proceed(r.m_Res.m_Events);

        if (nCount < proto::Event::s_Max)
        {
            Block::SystemState::Full sTip;
            m_WalletDB->get_History().get_Tip(sTip);

            SetEventsHeight(sTip.m_Height);
        }
        else
        {
            SetEventsHeight(p.m_Height);
            RequestEvents(); // maybe more events pending
        }
    }

    void Wallet::SetEventsHeight(Height h)
    {
        uintBigFor<Height>::Type var;
        var = h + 1; // we're actually saving the next
        storage::setVar(*m_WalletDB, s_szNextEvt, var);
    }

    Height Wallet::GetEventsHeightNext()
    {
        uintBigFor<Height>::Type var;
        if (!storage::getVar(*m_WalletDB, s_szNextEvt, var))
            return 0;

        Height h;
        var.Export(h);
        return h;
    }

    void Wallet::ProcessEventUtxo(const CoinID& cid, Height h, Height hMaturity, bool bAdd)
    {
        Coin c;
        c.m_ID = cid;

        bool bExists = m_WalletDB->findCoin(c);
        c.m_maturity = hMaturity;

        LOG_INFO() << "CoinID: " << c.m_ID << " Maturity=" << hMaturity << (bAdd ? " Confirmed" : " Spent") << ", Height=" << h;

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
                    LOG_INFO() << "CoinID: " << c.m_ID << " marked as Outgoing";
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

    void Wallet::ProcessEventShieldedUtxo(const proto::Event::Shielded& shieldedEvt, Height h)
    {
        auto shieldedCoin = m_WalletDB->getShieldedCoin(shieldedEvt.m_Key);
        if (!shieldedCoin)
        {
            shieldedCoin = ShieldedCoin{};
            shieldedCoin->m_Key = shieldedEvt.m_Key;
        }

        shieldedCoin->m_User = shieldedEvt.m_User;
        shieldedCoin->m_ID = shieldedEvt.m_ID;
        shieldedCoin->m_assetID = shieldedEvt.m_AssetID;
        shieldedCoin->m_value = shieldedEvt.m_Value;

        bool isAdd = 0 != (proto::Event::Flags::Add & shieldedEvt.m_Flags);
        if (isAdd)
        {
            shieldedCoin->m_confirmHeight = std::min(shieldedCoin->m_confirmHeight, h);
        }
        else
        {
            shieldedCoin->m_spentHeight = std::min(shieldedCoin->m_spentHeight, h);
        }

        m_WalletDB->saveShieldedCoin(*shieldedCoin);

        LOG_INFO() << "Shielded output, ID: " << shieldedEvt.m_ID << (isAdd ? " Confirmed" : " Spent") << ", Height=" << h;
    }

    void Wallet::OnRolledBack()
    {
        Block::SystemState::Full sTip;
        m_WalletDB->get_History().get_Tip(sTip);

        Block::SystemState::ID id;
        sTip.get_ID(id);
        LOG_INFO() << "Rolled back to " << id;

        m_WalletDB->get_History().DeleteFrom(sTip.m_Height + 1);
        m_WalletDB->rollbackConfirmedUtxo(sTip.m_Height);
        m_WalletDB->rollbackConfirmedShieldedUtxo(sTip.m_Height);
        m_WalletDB->rollbackAssets(sTip.m_Height);

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
                if (pTx->Rollback(sTip.m_Height))
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

    void Wallet::OnNewTip()
    {
        m_WalletDB->ShrinkHistory();

        Block::SystemState::Full sTip;
        get_tip(sTip);
        if (!sTip.m_Height)
            return; //?!

        Block::SystemState::ID id;
        sTip.get_ID(id);
        LOG_INFO() << "Sync up to " << id;

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
        LOG_INFO() << "Tip has not been changed";

        CheckSyncDone();

        ProcessStoredMessages();
    }

    void Wallet::getUtxoProof(const Coin& coin)
    {
        MyRequestUtxo::Ptr pReq(new MyRequestUtxo);
        pReq->m_CoinID = coin.m_ID;

        if (!m_WalletDB->get_CommitmentSafe(pReq->m_Msg.m_Utxo, coin.m_ID))
        {
            LOG_WARNING() << "You cannot get utxo commitment without private key";
            return;
        }

        LOG_DEBUG() << "Get utxo proof: " << pReq->m_Msg.m_Utxo;

        PostReqUnique(*pReq);
    }

    uint32_t Wallet::SyncRemains() const
    {
        size_t val =
#define THE_MACRO(type) m_Pending##type.size() +
            REQUEST_TYPES_Sync(THE_MACRO)
#undef THE_MACRO
            0;

        return static_cast<uint32_t>(val);
    }

    void Wallet::CheckSyncDone()
    {
        report_sync_progress();

        if (SyncRemains())
            return;

        m_LastSyncTotal = 0;

        saveKnownState();
    }

    void Wallet::saveKnownState()
    {
        Block::SystemState::Full sTip;
        get_tip(sTip);

        Block::SystemState::ID id;
        if (sTip.m_Height)
            sTip.get_ID(id);
        else
            ZeroObject(id);

        m_WalletDB->setSystemStateID(id);
        LOG_INFO() << "Current state is " << id;
        notifySyncProgress();

        if (!IsValidTimeStamp(sTip.m_TimeStamp))
        {
            // we are not ready to process transactions
            return;
        }
        std::unordered_set<BaseTransaction::Ptr> txSet;
        txSet.swap(m_TransactionsToUpdate);

        if (!txSet.empty())
        {
            AsyncContextHolder async(*this);
            for (auto it = txSet.begin(); txSet.end() != it; it++)
            {
                BaseTransaction::Ptr pTx = *it;
                if (m_ActiveTransactions.find(pTx->GetTxID()) != m_ActiveTransactions.end())
                    pTx->Update();
            }
        }
    }

    void Wallet::notifySyncProgress()
    {
        uint32_t n = SyncRemains();
        for (const auto sub : m_subscribers)
        {
            sub->onSyncProgress(m_LastSyncTotal - n, m_LastSyncTotal);
        }
    }

    void Wallet::report_sync_progress()
    {
        if (!m_LastSyncTotal)
            return;

        uint32_t nDone = m_LastSyncTotal - SyncRemains();
        assert(nDone <= m_LastSyncTotal);
        int p = static_cast<int>((nDone * 100) / m_LastSyncTotal);
        LOG_INFO() << "Synchronizing with node: " << p << "% (" << nDone << "/" << m_LastSyncTotal << ")";

        notifySyncProgress();
    }

    void Wallet::SendTransactionToNode(const TxID& txId, Transaction::Ptr data, SubTxID subTxID)
    {
        LOG_DEBUG() << txId << "[" << subTxID << "]" << " sending tx for registration";

#ifndef NDEBUG
        TxBase::Context::Params pars;
        TxBase::Context ctx(pars);
        ctx.m_Height.m_Min = m_WalletDB->getCurrentHeight();
        assert(data->IsValid(ctx));
#endif // NDEBUG

        MyRequestTransaction::Ptr pReq(new MyRequestTransaction);
        pReq->m_TxID = txId;
        pReq->m_SubTxID = subTxID;
        pReq->m_Msg.m_Transaction = std::move(data);

        PostReqUnique(*pReq);
    }

    void Wallet::register_tx(const TxID& txId, Transaction::Ptr data, SubTxID subTxID)
    {
        SendTransactionToNode(txId, data, subTxID);
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

    BaseTransaction::Ptr Wallet::GetTransaction(const WalletID& myID, const SetTxParameter& msg)
    {
        auto it = m_ActiveTransactions.find(msg.m_TxID);
        if (it != m_ActiveTransactions.end())
        {
            if (it->second->GetType() != msg.m_Type)
            {
                LOG_WARNING() << msg.m_TxID << " Parameters for invalid tx type";
            }
            if (WalletID peerID; it->second->GetParameter(TxParameterID::PeerID, peerID) && peerID != msg.m_From)
            {
                // if we already have PeerID, we should ignore messages from others
                return BaseTransaction::Ptr();
            }
            else
            {
                it->second->SetParameter(TxParameterID::PeerID, msg.m_From, false);
            }
            return it->second;
        }

        TxType type = TxType::Simple;
        if (storage::getTxParameter(*m_WalletDB, msg.m_TxID, TxParameterID::TransactionType, type))
        {
            // we return only active transactions
            return BaseTransaction::Ptr();
        }

        if (msg.m_Type == TxType::AtomicSwap)
        {
            // we don't create swap from SBBS message
            return BaseTransaction::Ptr();
        }

        bool isSender = false;
        if (!msg.GetParameter(TxParameterID::IsSender, isSender) || isSender == true)
        {
            return BaseTransaction::Ptr();
        }

        auto t = ConstructTransactionFromParameters(msg);
        if (t)
        {
            t->SetParameter(TxParameterID::TransactionType, msg.m_Type, false);
            t->SetParameter(TxParameterID::CreateTime, getTimestamp(), false);
            t->SetParameter(TxParameterID::MyID, myID, false);
            t->SetParameter(TxParameterID::PeerID, msg.m_From, false);
            t->SetParameter(TxParameterID::IsInitiator, false, false);
            t->SetParameter(TxParameterID::Status, TxStatus::Pending, true);

            auto address = m_WalletDB->getAddress(myID);
            if (address.is_initialized())
            {
                ByteBuffer message(address->m_label.begin(), address->m_label.end());
                t->SetParameter(TxParameterID::Message, message);
            }

            MakeTransactionActive(t);
        }
        return t;
    }

    BaseTransaction::Ptr Wallet::ConstructTransaction(const TxID& id, TxType type)
    {
        auto it = m_TxCreators.find(type);
        if (it == m_TxCreators.end())
        {
            LOG_WARNING() << id << " Unsupported type of transaction: " << static_cast<int>(type);
            return wallet::BaseTransaction::Ptr();
        }

        return it->second->Create(*this, m_WalletDB, id);
    }

    BaseTransaction::Ptr Wallet::ConstructTransactionFromParameters(const SetTxParameter& msg)
    {
        auto it = m_TxCreators.find(msg.m_Type);
        if (it == m_TxCreators.end())
        {
            LOG_WARNING() << msg.m_TxID << " Unsupported type of transaction: " << static_cast<int>(msg.m_Type);
            return wallet::BaseTransaction::Ptr();
        }

        return it->second->Create(*this, m_WalletDB, msg.m_TxID);
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
            LOG_ERROR() << *parameters.GetTxID() << " Unsupported type of transaction: " << static_cast<int>(*type);
            return BaseTransaction::Ptr();
        }

        auto completedParameters = it->second->CheckAndCompleteParameters(parameters);

        if (auto peerID = parameters.GetParameter(TxParameterID::PeerSecureWalletID); peerID)
        {
            auto myID = parameters.GetParameter<WalletID>(TxParameterID::MyID);
            if (myID)
            {
                auto address = m_WalletDB->getAddress(*myID);
                if (address)
                {
                    completedParameters.SetParameter(TxParameterID::MySecureWalletID, address->m_Identity);
                }
            }
        }

        auto newTx = it->second->Create(*this, m_WalletDB, *parameters.GetTxID());
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
            Block::SystemState::Full sTip;
            get_tip(sTip);
            return IsValidTimeStamp(sTip.m_TimeStamp);
        }
        return true; // to allow made air-gapped transactions
    }

    void Wallet::RequestStateSummary()
    {
        MyRequestStateSummary::Ptr pReq(new MyRequestStateSummary);
        PostReqUnique(*pReq);
    }
}
