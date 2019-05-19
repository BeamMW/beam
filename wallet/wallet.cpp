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
#include "bitcoin/bitcoind017.h"
#include "bitcoin/bitcoin_side.h"
#include "litecoin/litecoind016.h"
#include "litecoin/litecoin_side.h"

namespace beam
{
    using namespace wallet;
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
    }

    int WalletID::cmp(const WalletID& x) const
    {
        int n = m_Channel.cmp(x.m_Channel);
        if (n)
            return n;
        return m_Pk.cmp(x.m_Pk);
    }

    bool WalletID::FromBuf(const ByteBuffer& x)
    {
        if (x.size() > sizeof(*this))
            return false;

        typedef uintBig_t<sizeof(*this)> BigSelf;
        static_assert(sizeof(BigSelf) == sizeof(*this), "");

        *reinterpret_cast<BigSelf*>(this) = Blob(x);
        return true;
    }

    bool WalletID::FromHex(const std::string& s)
    {
        bool bValid = true;
        ByteBuffer bb = from_hex(s, &bValid);

        return bValid && FromBuf(bb);
    }

    bool WalletID::IsValid() const
    {
        Point::Native p;
        return proto::ImportPeerID(p, m_Pk);
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

    Wallet::Wallet(IWalletDB::Ptr walletDB, TxCompletedAction&& action, UpdateCompletedAction&& updateCompleted)
        : m_WalletDB{ walletDB }
        , m_TxCompletedAction{move(action)}
        , m_UpdateCompleted{move(updateCompleted)}
        , m_LastSyncTotal(0)
        , m_OwnedNodesOnline(0)
    {
        assert(walletDB);
        ResumeAllTransactions();
    }


    // Fly client implementation

    void Wallet::get_Kdf(Key::IKdf::Ptr& pKdf)
    {
        pKdf = m_WalletDB->get_MasterKdf();
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

    // 

    void Wallet::SetNodeEndpoint(std::shared_ptr<proto::FlyClient::INetwork> nodeEndpoint)
    {
        m_NodeEndpoint = nodeEndpoint;
    }

    void Wallet::AddMessageEndpoint(IWalletMessageEndpoint::Ptr endpoint)
    {
        m_MessageEndpoints.insert(endpoint);
    }


    // Atomic Swap related methods
    // TODO: Refactor
    void Wallet::initBitcoin(io::Reactor& reactor, const std::string& userName, const std::string& pass, const io::Address& address, Amount feeRate, bool mainnet)
    {
        m_bitcoinBridge = make_shared<Bitcoind017>(reactor, userName, pass, address, feeRate, mainnet);
    }

    void Wallet::initLitecoin(io::Reactor& reactor, const std::string& userName, const std::string& pass, const io::Address& address, Amount feeRate,  bool mainnet)
    {
        m_litecoinBridge = make_shared<Litecoind016>(reactor, userName, pass, address, feeRate, mainnet);
    }

    void Wallet::initSwapConditions(Amount beamAmount, Amount swapAmount, AtomicSwapCoin swapCoin, bool isBeamSide)
    {
        m_swapConditions.push_back(SwapConditions{ beamAmount, swapAmount, swapCoin, isBeamSide });
    }

    Wallet::~Wallet()
    {
        // clear all requests
#define THE_MACRO(type, msgOut, msgIn) \
        while (!m_Pending##type.empty()) \
            DeleteReq(*m_Pending##type.begin());

        REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO
    }

    TxID Wallet::transfer_money(const WalletID& from, const WalletID& to, Amount amount, Amount fee, bool sender, Height lifetime, Height responseTime, ByteBuffer&& message, bool saveReceiver)
    {
        return transfer_money(from, to, AmountList{ amount }, fee, {}, sender, lifetime, responseTime, move(message), saveReceiver);
    }

    TxID Wallet::transfer_money(const WalletID& from, const WalletID& to, Amount amount, Amount fee, const CoinIDList& coins, bool sender, Height lifetime, Height responseTime, ByteBuffer&& message, bool saveReceiver)
    {
        return transfer_money(from, to, AmountList{ amount }, fee, coins, sender, lifetime, responseTime, move(message), saveReceiver);
    }

    // TODO: Change flag saveReceiver to separate function for handling receiver address separately
    TxID Wallet::transfer_money(const WalletID& from, const WalletID& to, const AmountList& amountList, Amount fee, const CoinIDList& coins, bool sender, Height lifetime, Height responseTime, ByteBuffer&& message, bool saveReceiver)
    {
        auto receiverAddr = m_WalletDB->getAddress(to);

        if (receiverAddr)
        {
            if (receiverAddr->m_OwnID && receiverAddr->isExpired())
            {
                LOG_INFO() << "Can't send to the expired address.";
                throw AddressExpiredException();
            }

            // update address comment if changed
            auto messageStr = std::string(message.begin(), message.end());

            if (messageStr != receiverAddr->m_label)
            {
                receiverAddr->m_label = messageStr;
                m_WalletDB->saveAddress(*receiverAddr);
            }
        }
        else if (saveReceiver)
        {
            WalletAddress address;
            address.m_walletID = to;
            address.m_createTime = getTimestamp();
            address.m_label = std::string(message.begin(), message.end());

            m_WalletDB->saveAddress(address);
        }

        TxID txID = wallet::GenerateTxID();
        auto tx = constructTransaction(txID, TxType::Simple);

        tx->SetParameter(TxParameterID::TransactionType, TxType::Simple, false);
        tx->SetParameter(TxParameterID::Lifetime, lifetime, false);
        tx->SetParameter(TxParameterID::PeerResponseHeight, responseTime); 
        tx->SetParameter(TxParameterID::IsInitiator, true, false);
        tx->SetParameter(TxParameterID::AmountList, amountList, false);
        tx->SetParameter(TxParameterID::PreselectedCoins, coins, false);

        TxDescription txDescription;

        txDescription.m_txId = txID;
        txDescription.m_amount = std::accumulate(amountList.begin(), amountList.end(), 0ULL);
        txDescription.m_fee = fee;
        txDescription.m_peerId = to;
        txDescription.m_myId = from;
        txDescription.m_message = move(message);
        txDescription.m_createTime = getTimestamp();
        txDescription.m_sender = sender;
        txDescription.m_status = TxStatus::Pending;
        txDescription.m_selfTx = (receiverAddr && receiverAddr->m_OwnID);
        m_WalletDB->saveTx(txDescription);

        m_Transactions.emplace(txID, tx);

        updateTransaction(txID);

        return txID;
    }

    TxID Wallet::split_coins(const WalletID& from, const AmountList& amountList, Amount fee, bool sender, Height lifetime, Height responseTime,  ByteBuffer&& message)
    {
        return transfer_money(from, from, amountList, fee, {}, sender, lifetime, responseTime, move(message));
    }

    TxID Wallet::swap_coins(const WalletID& from, const WalletID& to, Amount amount, Amount fee, wallet::AtomicSwapCoin swapCoin,
        Amount swapAmount, bool isBeamSide/*=true*/, Height lifetime/* = kDefaultTxLifetime*/, Height responseTime/* = kDefaultTxResponseTime*/)
    {
        auto txID = wallet::GenerateTxID();
        auto tx = constructTransaction(txID, TxType::AtomicSwap);

        tx->SetParameter(TxParameterID::TransactionType, TxType::AtomicSwap, false);
        tx->SetParameter(TxParameterID::CreateTime, getTimestamp(), false);
        tx->SetParameter(TxParameterID::Amount, amount, false);
        tx->SetParameter(TxParameterID::Fee, fee, false);
        tx->SetParameter(TxParameterID::Lifetime, lifetime, false);
        tx->SetParameter(TxParameterID::PeerID, to, false);

        // Must be reset on first Update when we already have correct current height.
        tx->SetParameter(TxParameterID::PeerResponseHeight, responseTime);
        tx->SetParameter(TxParameterID::MyID, from, false);
        tx->SetParameter(TxParameterID::IsSender, isBeamSide, false);
        tx->SetParameter(TxParameterID::IsInitiator, true, false);
        tx->SetParameter(TxParameterID::Status, TxStatus::Pending, true);

        tx->SetParameter(TxParameterID::AtomicSwapCoin, swapCoin, false);
        tx->SetParameter(TxParameterID::AtomicSwapAmount, swapAmount, false);
        tx->SetParameter(TxParameterID::AtomicSwapIsBeamSide, isBeamSide, false);

        m_Transactions.emplace(txID, tx);

        updateTransaction(txID);

        return txID;
    }

    // TODO: Rename to Rescan ?
    // Reset wallet state and rescan the blockchain
    void Wallet::Refresh()
    {
        m_WalletDB->clear();
        Block::SystemState::ID id;
        ZeroObject(id);
        m_WalletDB->setSystemStateID(id);

        SetUtxoEventsHeight(0);
        RequestUtxoEvents();
        RefreshTransactions();
    }

    void Wallet::RefreshTransactions()
    {
        auto txs = m_WalletDB->getTxHistory(); // get list of ALL transactions
        for (auto& tx : txs)
        {
            // For all transactions that are not currently in the 'active' tx list
            if (m_Transactions.find(tx.m_txId) == m_Transactions.end())
            {
                // Reconstruct tx with reset parameters and add it to the active list
                auto t = constructTransaction(tx.m_txId, tx.m_txType);
                if (t->SetParameter(TxParameterID::KernelProofHeight, Height(0), false)
                    && t->SetParameter(TxParameterID::KernelUnconfirmedHeight, Height(0), false))
                {
                    m_Transactions.emplace(tx.m_txId, t);
                }
            }
        }

        // Update all transactions
        auto t = m_Transactions;
        AsyncContextHolder holder(*this);
        for (auto& p : t)
        {
            auto tx = p.second;
            tx->Update();
        }
    }

    void Wallet::ResumeTransaction(const TxDescription& tx)
    {
        if (tx.canResume() && m_Transactions.find(tx.m_txId) == m_Transactions.end())
        {
            auto t = constructTransaction(tx.m_txId, tx.m_txType);

            m_Transactions.emplace(tx.m_txId, t);
            UpdateOnSynced(t);
        }
    }

    void Wallet::ResumeAllTransactions()
    {
        auto txs = m_WalletDB->getTxHistory();
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

		wallet::BaseTransaction::Ptr pGuard;

        auto it = m_Transactions.find(txID);
        if (it != m_Transactions.end())
        {
			pGuard.swap(it->second);
            m_Transactions.erase(it);
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
    // @param subTxID : wallet::SubTxID - in case of the complex transaction, there could be sub transactions
    void Wallet::confirm_kernel(const TxID& txID, const Merkle::Hash& kernelID, wallet::SubTxID subTxID)
    {
        if (auto it = m_Transactions.find(txID); it != m_Transactions.end())
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
    // @param subTxID : wallet::SubTxID - in case of the complex transaction, there could be sub transactions
    void Wallet::get_kernel(const TxID& txID, const Merkle::Hash& kernelID, wallet::SubTxID subTxID)
    {
        if (auto it = m_Transactions.find(txID); it != m_Transactions.end())
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
    // TODO: make SetTxParameter const reference
    void Wallet::send_tx_params(const WalletID& peerID, SetTxParameter&& msg)
    {
        for (auto& endpoint : m_MessageEndpoints)
        {
            endpoint->Send(peerID, msg);
        }
    }

    // Implementation of the INegotiatorGateway::UpdateOnNextTip
    void Wallet::UpdateOnNextTip(const TxID& txID)
    {
        auto it = m_Transactions.find(txID);
        if (it != m_Transactions.end())
        {
            UpdateOnNextTip(it->second);
        }
    }

    // Implementation of the INegotiatorGateway::GetSecondSide
    SecondSide::Ptr Wallet::GetSecondSide(const TxID& txID) const
    {
        auto it = m_Transactions.find(txID);
        if (it != m_Transactions.end())
        {
            TxType type = it->second->GetMandatoryParameter<TxType>(TxParameterID::TransactionType);

            if (type != TxType::AtomicSwap)
            {
                LOG_ERROR() << txID << "Transaction has invalid type.";
                return nullptr;
            }

            auto swapCoin = it->second->GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);

            if (swapCoin == AtomicSwapCoin::Bitcoin)
            {
                if (!m_bitcoinBridge)
                {
                    LOG_ERROR() << "Bitcoin bridge is not initialized";
                    return nullptr;
                }

                bool isBeamSide = it->second->GetMandatoryParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
                return std::make_shared<BitcoinSide>(*it->second, m_bitcoinBridge, isBeamSide);
            }

            if (swapCoin == AtomicSwapCoin::Litecoin)
            {
                if (!m_litecoinBridge)
                {
                    LOG_ERROR() << "Litecoin bridge is not initialized";
                    return nullptr;
                }

                bool isBeamSide = it->second->GetMandatoryParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
                return std::make_shared<LitecoinSide>(*it->second, m_litecoinBridge, isBeamSide);
            }
        }

        LOG_ERROR() << "Transaction is absent in wallet.";

        return nullptr;
    }

    void Wallet::OnWalletMessage(const WalletID& myID, wallet::SetTxParameter&& msg)
    {
        auto t = getTransaction(myID, msg);
        if (!t)
        {
            return;
        }
        bool txChanged = false;
        SubTxID subTxID = kDefaultSubTxID;

        for (const auto& p : msg.m_Parameters)
        {
            if (p.first == TxParameterID::SubTxIndex)
            {
                // change subTxID
                Deserializer d;
                d.reset(p.second.data(), p.second.size());
                d & subTxID;
                continue;
            }

            if (p.first < TxParameterID::PrivateFirstParam)
            {
                txChanged |= t->SetParameter(p.first, p.second, subTxID);
            }
            else
            {
                LOG_WARNING() << "Attempt to set private tx parameter";
            }
        }
        if (txChanged)
        {
            updateTransaction(msg.m_TxID);
        }
    }

    void Wallet::OnRequestComplete(MyRequestTransaction& r)
    {
        LOG_DEBUG() << r.m_TxID << "[" << r.m_SubTxID << "]" << " register status " << static_cast<uint32_t>(r.m_Res.m_Value);
        
        auto it = m_Transactions.find(r.m_TxID);
        if (it != m_Transactions.end())
        {
            it->second->SetParameter(TxParameterID::TransactionRegistered, r.m_Res.m_Value, r.m_SubTxID);
            updateTransaction(r.m_TxID);
        }
    }

    // Implementation of the IWallet::cancel_tx
    void Wallet::cancel_tx(const TxID& txId)
    {
        LOG_INFO() << txId << " Canceling tx";

        if (auto it = m_Transactions.find(txId); it != m_Transactions.end())
        {
            it->second->Cancel();
        }
        else
        {
            m_WalletDB->deleteTx(txId);
        }
    }

    // Implementation of the IWallet::delete_tx
    void Wallet::delete_tx(const TxID& txId)
    {
        LOG_INFO() << "deleting tx " << txId;
        if (auto it = m_Transactions.find(txId); it == m_Transactions.end())
        {
            m_WalletDB->deleteTx(txId);
        }
        else
        {
            LOG_WARNING() << "Cannot delete running transaction";
        }
    }

    void Wallet::updateTransaction(const TxID& txID)
    {
        auto it = m_Transactions.find(txID);
        if (it != m_Transactions.end())
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

    void Wallet::UpdateOnNextTip(wallet::BaseTransaction::Ptr tx)
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
        evt.m_Height = MaxHeight; // not used, relevant only for spend events

        ProcessUtxoEvent(evt); // uniform processing for all confirmed utxos
    }

    void Wallet::OnRequestComplete(MyRequestKernel& r)
    {
        auto it = m_Transactions.find(r.m_TxID);
        if (m_Transactions.end() == it)
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

    void Wallet::OnRequestComplete(MyRequestKernel2 & r)
    {
        auto it = m_Transactions.find(r.m_TxID);
        if (m_Transactions.end() == it)
        {
            return;
        }
        auto tx = it->second;

        if (r.m_Res.m_Kernel && r.m_Res.m_Kernel->m_pHashLock)
        {
            tx->SetParameter(TxParameterID::PreImage, r.m_Res.m_Kernel->m_pHashLock->m_Preimage, r.m_SubTxID);
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
        const std::vector<proto::UtxoEvent>& v = r.m_Res.m_Events;
		for (size_t i = 0; i < v.size(); i++)
		{
			const proto::UtxoEvent& evt = v[i];

			// filter-out false positives
			Scalar::Native sk;
			Point comm;
			m_WalletDB->calcCommitment(sk, comm, evt.m_Kidv);

			if (comm == evt.m_Commitment)
				ProcessUtxoEvent(evt);
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
        wallet::setVar(*m_WalletDB, s_szNextUtxoEvt, var);
    }

    Height Wallet::GetUtxoEventsHeightNext()
    {
        uintBigFor<Height>::Type var;
        if (!wallet::getVar(*m_WalletDB, s_szNextUtxoEvt, var))
            return 0;

        Height h;
        var.Export(h);
        return h;
    }

    void Wallet::ProcessUtxoEvent(const proto::UtxoEvent& evt)
    {
        Coin c;
        c.m_ID = evt.m_Kidv;

        bool bExists = m_WalletDB->find(c);
		c.m_maturity = evt.m_Maturity;

        LOG_INFO() << "CoinID: " << evt.m_Kidv << " Maturity=" << evt.m_Maturity << (evt.m_Added ? " Confirmed" : " Spent");

        if (evt.m_Added)
			c.m_confirmHeight = std::min(c.m_confirmHeight, evt.m_Height); // in case of std utxo proofs - the event height may be bigger than actual utxo height
        else
        {
            if (!bExists)
                return; // should alert!

			c.m_spentHeight = std::min(c.m_spentHeight, evt.m_Height); // reported spend height may be bigger than it actuall was (in case of macroblocks)
		}

        m_WalletDB->save(c);
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

        ResumeAllTransactions();

        for (auto it = m_Transactions.begin(); m_Transactions.end() != it; it++)
        {
            const auto& pTx = it->second;

            if (pTx->Rollback(sTip.m_Height))
            {
                UpdateOnSynced(pTx);
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

		Scalar::Native sk;
		m_WalletDB->calcCommitment(sk, pReq->m_Msg.m_Utxo, cid);

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
        std::unordered_set<wallet::BaseTransaction::Ptr> txSet;
        txSet.swap(m_TransactionsToUpdate);

        AsyncContextHolder async(*this);
        for (auto it = txSet.begin(); txSet.end() != it; it++)
        {
            wallet::BaseTransaction::Ptr pTx = *it;
            if (m_Transactions.find(pTx->GetTxID()) != m_Transactions.end())
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

    void Wallet::register_tx(const TxID& txId, Transaction::Ptr data, wallet::SubTxID subTxID)
    {
        LOG_VERBOSE() << txId << "[" << subTxID << "]" << " sending tx for registration";

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

    void Wallet::subscribe(IWalletObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());

        m_subscribers.push_back(observer);

        m_WalletDB->subscribe(observer);
    }

    void Wallet::unsubscribe(IWalletObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);

        assert(it != m_subscribers.end());

        m_subscribers.erase(it);

        m_WalletDB->unsubscribe(observer);
    }

    wallet::BaseTransaction::Ptr Wallet::getTransaction(const WalletID& myID, const wallet::SetTxParameter& msg)
    {
        auto it = m_Transactions.find(msg.m_TxID);
        if (it != m_Transactions.end())
        {
            if (it->second->GetType() != msg.m_Type)
            {
                LOG_WARNING() << msg.m_TxID << " Parameters for invalid tx type";
            }
            return it->second;
        }

        TxType type = TxType::Simple;
        if (wallet::getTxParameter(*m_WalletDB, msg.m_TxID, TxParameterID::TransactionType, type))
        {
            // we return only active transactions
            return BaseTransaction::Ptr();
        }

        bool isSender = false;
        if (!msg.GetParameter(TxParameterID::IsSender, isSender) || (isSender == true && msg.m_Type != TxType::AtomicSwap))
        {
            return BaseTransaction::Ptr();
        }

        if (msg.m_Type == TxType::AtomicSwap)
        {
            if (m_swapConditions.empty())
            {
                LOG_DEBUG() << msg.m_TxID << " Swap rejected. Swap conditions aren't initialized.";
                return BaseTransaction::Ptr();
            }

            // validate swapConditions
            Amount amount = 0;
            Amount swapAmount = 0;
            wallet::AtomicSwapCoin swapCoin = wallet::AtomicSwapCoin::Bitcoin;
            bool isBeamSide = 0;

            bool result = msg.GetParameter(TxParameterID::Amount, amount) &&
                msg.GetParameter(TxParameterID::AtomicSwapAmount, swapAmount) &&
                msg.GetParameter(TxParameterID::AtomicSwapCoin, swapCoin) &&
                msg.GetParameter(TxParameterID::AtomicSwapIsBeamSide, isBeamSide);

            auto idx = std::find(m_swapConditions.begin(), m_swapConditions.end(), SwapConditions{ amount, swapAmount, swapCoin, isBeamSide });

            if (!result || idx == m_swapConditions.end())
            {
                LOG_DEBUG() << msg.m_TxID << " Swap rejected. Invalid conditions.";
                return BaseTransaction::Ptr();
            }

            m_swapConditions.erase(idx);

            LOG_DEBUG() << msg.m_TxID << " Swap accepted.";
        }

        auto t = constructTransaction(msg.m_TxID, msg.m_Type);

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

        m_Transactions.emplace(msg.m_TxID, t);
        return t;
    }

    wallet::BaseTransaction::Ptr Wallet::constructTransaction(const TxID& id, TxType type)
    {
        switch (type)
        {
        case TxType::Simple:
             return make_shared<SimpleTransaction>(*this, m_WalletDB, id);
        case TxType::AtomicSwap:
        {
            return make_shared<AtomicSwapTransaction>(*this, m_WalletDB, id);
        }
        }
        return wallet::BaseTransaction::Ptr();
    }

    void Wallet::ProcessStoredMessages()
    {
        if (m_MessageEndpoints.empty())
        {
            return;
        }
        {
            auto messages = m_WalletDB->getWalletMessages();
            for (auto& message : messages)
            {
                for (auto& endpoint : m_MessageEndpoints)
                {
                    endpoint->SendEncryptedMessage(message.m_PeerID, message.m_Message);
                }
                m_WalletDB->deleteWalletMessage(message.m_ID);
            }
        }
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
