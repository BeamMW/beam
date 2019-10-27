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
#include "swaps/swap_transaction.h"

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
        // Check current time with the timestamp of last received block
        // If it is more than 10 minutes, the walelt is considered not in sync
        bool IsValidTimeStamp(Timestamp currentBlockTime_s)
        {
            Timestamp currentTime_s = getTimestamp();
            const Timestamp tolerance_s = 60 * 10; // 10 minutes tolerance.
            currentBlockTime_s += tolerance_s;

            if (currentTime_s > currentBlockTime_s)
            {
                LOG_INFO() << "It seems that node is not up to date";
                return false;
            }
            return true;
        }

        bool ApplyTransactionParameters(BaseTransaction::Ptr tx, const PackedTxParameters& parameters, bool allowPrivate = false)
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

    const char Wallet::s_szNextUtxoEvt[] = "NextUtxoEvent";

    Wallet::Wallet(IWalletDB::Ptr walletDB, IPrivateKeyKeeper::Ptr keyKeeper, TxCompletedAction&& action, UpdateCompletedAction&& updateCompleted)
        : m_WalletDB{ walletDB }
        , m_TxCompletedAction{ move(action) }
        , m_UpdateCompleted{ move(updateCompleted) }
        , m_LastSyncTotal(0)
        , m_OwnedNodesOnline(0)
        , m_KeyKeeper(keyKeeper)
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
                RequestUtxoEvents(); // maybe time to refresh UTXOs
        }
        else
        {
            assert(m_OwnedNodesOnline); // check that m_OwnedNodesOnline is positive number
            if (!--m_OwnedNodesOnline)
                AbortUtxoEvents();
        }
    }

    Block::SystemState::IHistory& Wallet::get_History()
    {
        return m_WalletDB->get_History();
    }

    void Wallet::SetNodeEndpoint(std::shared_ptr<proto::FlyClient::INetwork> nodeEndpoint)
    {
        m_NodeEndpoint = nodeEndpoint;
    }

    void Wallet::AddMessageEndpoint(IWalletMessageEndpoint::Ptr endpoint)
    {
        m_MessageEndpoints.insert(endpoint);
    }

    // TODO: Rename to Rescan ?
    // Reset wallet state and rescan the blockchain
    void Wallet::Refresh()
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
        Block::SystemState::ID id;
        ZeroObject(id);
        m_WalletDB->setSystemStateID(id);

        // Restore Incoming coins of active transactions
        m_WalletDB->saveCoins(ocoins);

        storage::setVar(*m_WalletDB, s_szNextUtxoEvt, 0);
        RequestUtxoEvents();
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
            getUtxoProof(coin.m_ID);
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

    bool Wallet::MyRequestTransaction::operator < (const MyRequestTransaction& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestUtxoEvents::operator < (const MyRequestUtxoEvents& x) const
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
            pVal->m_Msg.m_Fetch = true;
            pVal->m_Msg.m_ID = kernelID;

            if (PostReqUnique(*pVal))
            {
                LOG_INFO() << txID << "[" << subTxID << "]" << " Get details for kernel: " << pVal->m_Msg.m_ID;
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

        if (ApplyTransactionParameters(t, msg.m_Parameters))
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
            auto tx = it->second;
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
        else
        {
            LOG_DEBUG() << txID << " Unexpected event";
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

        proto::UtxoEvent evt;
        evt.m_Added = 1;
        evt.m_Kidv = r.m_CoinID;
        evt.m_Maturity = proof.m_State.m_Maturity;
        evt.m_Height = evt.m_Maturity; // we don't know the real height, but it'll be used for logging only. For standard outputs maturity and height are the same

        ProcessUtxoEvent(evt); // uniform processing for all confirmed utxos
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
                AsyncContextHolder holder(*this);
                tx->Update();
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
    }

    void Wallet::OnRequestComplete(MyRequestBbsMsg& r)
    {
        assert(false);
    }

    void Wallet::RequestUtxoEvents()
    {
        if (!m_OwnedNodesOnline)
            return;

        Block::SystemState::Full sTip;
        m_WalletDB->get_History().get_Tip(sTip);

        Height h = GetUtxoEventsHeightNext();
        assert(h <= sTip.m_Height + 1);
        if (h > sTip.m_Height)
            return;

        if (!m_PendingUtxoEvents.empty())
        {
            if (m_PendingUtxoEvents.begin()->m_Msg.m_HeightMin == h)
                return; // already pending
            DeleteReq(*m_PendingUtxoEvents.begin());
        }

        MyRequestUtxoEvents::Ptr pReq(new MyRequestUtxoEvents);
        pReq->m_Msg.m_HeightMin = h;
        PostReqUnique(*pReq);
    }

    void Wallet::AbortUtxoEvents()
    {
        if (!m_PendingUtxoEvents.empty())
            DeleteReq(*m_PendingUtxoEvents.begin());
    }

    void Wallet::OnRequestComplete(MyRequestUtxoEvents& r)
    {
        std::vector<proto::UtxoEvent>& v = r.m_Res.m_Events;

        function<Point(const Key::IDV&)> commitmentFunc;
        if (m_KeyKeeper)
        {
            commitmentFunc = [this](const auto& kidv) {return m_KeyKeeper->GeneratePublicKeySync(kidv, true); };
        }
        else if (auto ownerKdf = m_WalletDB->get_OwnerKdf(); ownerKdf)
        {
            commitmentFunc = [ownerKdf](const auto& kidv)
            {
                Point::Native pt;
                SwitchCommitment sw;

                sw.Recover(pt, *ownerKdf, kidv);
                Point commitment = pt;
                return commitment;
            };
        }

        for (size_t i = 0; i < v.size(); i++)
        {
            auto& event = v[i];

            // filter-out false positives
            if (commitmentFunc)
            {
                Point commitment = commitmentFunc(event.m_Kidv);
                if (commitment == event.m_Commitment)
                    ProcessUtxoEvent(event);
                else
                {
                    // Is it BB2.1?
                    if (event.m_Kidv.IsBb21Possible())
                    {
                        event.m_Kidv.set_WorkaroundBb21();

                        commitment = commitmentFunc(event.m_Kidv);
                        if (commitment == event.m_Commitment)
                            ProcessUtxoEvent(event);
                    }
                }
            }
        }

        if (r.m_Res.m_Events.size() < proto::UtxoEvent::s_Max)
        {
            Block::SystemState::Full sTip;
            m_WalletDB->get_History().get_Tip(sTip);

            SetUtxoEventsHeight(sTip.m_Height);
        }
        else
        {
            SetUtxoEventsHeight(r.m_Res.m_Events.back().m_Height);
            RequestUtxoEvents(); // maybe more events pending
        }
    }

    void Wallet::SetUtxoEventsHeight(Height h)
    {
        uintBigFor<Height>::Type var;
        var = h + 1; // we're actually saving the next
        storage::setVar(*m_WalletDB, s_szNextUtxoEvt, var);
    }

    Height Wallet::GetUtxoEventsHeightNext()
    {
        uintBigFor<Height>::Type var;
        if (!storage::getVar(*m_WalletDB, s_szNextUtxoEvt, var))
            return 0;

        Height h;
        var.Export(h);
        return h;
    }

    void Wallet::ProcessUtxoEvent(const proto::UtxoEvent& evt)
    {
        Coin c;
        c.m_ID = evt.m_Kidv;

        bool bExists = m_WalletDB->findCoin(c);
        c.m_maturity = evt.m_Maturity;

        LOG_INFO() << "CoinID: " << evt.m_Kidv << " Maturity=" << evt.m_Maturity << (evt.m_Added ? " Confirmed" : " Spent") << ", Height=" << evt.m_Height;

        if (evt.m_Added)
        {
            c.m_confirmHeight = std::min(c.m_confirmHeight, evt.m_Height); // in case of std utxo proofs - the event height may be bigger than actual utxo height

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
                    LOG_INFO() << "CoinID: " << evt.m_Kidv << " marked as Outgoing";
                }
            }
        }
        else
        {
            if (!bExists)
                return; // should alert!

            c.m_spentHeight = std::min(c.m_spentHeight, evt.m_Height); // reported spend height may be bigger than it actuall was (in case of macroblocks)
        }

        m_WalletDB->saveCoin(c);
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

        Height h = GetUtxoEventsHeightNext();
        if (h > sTip.m_Height + 1)
            SetUtxoEventsHeight(sTip.m_Height);
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

        RequestUtxoEvents();

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

    void Wallet::getUtxoProof(const Coin::ID& cid)
    {
        MyRequestUtxo::Ptr pReq(new MyRequestUtxo);
        pReq->m_CoinID = cid;

        if (!m_KeyKeeper)
        {
            LOG_WARNING() << "You cannot get utxo commitment without private key";
            return;
        }
        pReq->m_Msg.m_Utxo = m_KeyKeeper->GeneratePublicKeySync(cid, true);

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

        Block::SystemState::ID currentID;
        m_WalletDB->getSystemStateID(currentID);

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

        AsyncContextHolder async(*this);
        for (auto it = txSet.begin(); txSet.end() != it; it++)
        {
            BaseTransaction::Ptr pTx = *it;
            if (m_ActiveTransactions.find(pTx->GetTxID()) != m_ActiveTransactions.end())
                pTx->Update();
        }
    }

    void Wallet::notifySyncProgress()
    {
        uint32_t n = SyncRemains();
        for (auto sub : m_subscribers)
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
            it->second->SetParameter(TxParameterID::PeerID, msg.m_From, false);
            return it->second;
        }

        TxType type = TxType::Simple;
        if (storage::getTxParameter(*m_WalletDB, msg.m_TxID, TxParameterID::TransactionType, type))
        {
            // we return only active transactions
            return BaseTransaction::Ptr();
        }

        bool isSender = false;
        if (!msg.GetParameter(TxParameterID::IsSender, isSender) || (isSender == true && msg.m_Type != TxType::AtomicSwap))
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

        return it->second->Create(*this, m_WalletDB, m_KeyKeeper, id);
    }

    BaseTransaction::Ptr Wallet::ConstructTransactionFromParameters(const SetTxParameter& msg)
    {
        auto it = m_TxCreators.find(msg.m_Type);
        if (it == m_TxCreators.end())
        {
            LOG_WARNING() << msg.m_TxID << " Unsupported type of transaction: " << static_cast<int>(msg.m_Type);
            return wallet::BaseTransaction::Ptr();
        }

        return it->second->Create(*this, m_WalletDB, m_KeyKeeper, msg.m_TxID);
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

        auto newTx = it->second->Create(*this, m_WalletDB, m_KeyKeeper, *parameters.GetTxID());
        ApplyTransactionParameters(newTx, completedParameters.Pack());
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
                endpoint->SendEncryptedMessage(message.m_PeerID, message.m_Message);
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
}
