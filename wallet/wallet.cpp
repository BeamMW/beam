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

// TODO: getrandom not available until API 28 in the Android NDK 17b
// https://github.com/boostorg/uuid/issues/76
#if defined(__ANDROID__)
#define BOOST_UUID_RANDOM_PROVIDER_DISABLE_GETRANDOM 1
#endif

#include "core/ecc_native.h"
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "utility/helpers.h"
#include "swap_transaction.h"
#include <algorithm>
#include <random>
#include <iomanip>

namespace std
{
    string to_string(const beam::WalletID& id)
    {
        return beam::to_hex(id.m_pData, id.nBytes);
    }
}

namespace beam
{
    using namespace wallet;
    using namespace std;
    using namespace ECC;

    std::ostream& operator<<(std::ostream& os, const TxID& uuid)
    {
        os << "[" << to_hex(uuid.data(), uuid.size()) << "]";
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const PrintableAmount& amount)
    {
        const string_view beams{" beams " };
        const string_view chattles{ " groth " };
        auto width = os.width();

        if (amount.m_showPoint)
        {
            os << setw(width - beams.length() - 1) << Amount(amount.m_value / Rules::Coin)
                << "."
                << (amount.m_value % Rules::Coin)
                << beams.data();
            return os;
        }
        
        if (amount.m_value >= Rules::Coin)
        {
            os << setw(width - beams.length()) << Amount(amount.m_value / Rules::Coin) << beams.data();
        }
        Amount c = amount.m_value % Rules::Coin;
        if (c > 0 || amount.m_value == 0)
        {
            os << setw(width - chattles.length()) << c << chattles.data();
        }
        return os;
    }

    namespace wallet
    {
        pair<Scalar::Native, Scalar::Native> splitKey(const Scalar::Native& key, uint64_t index)
        {
            pair<Scalar::Native, Scalar::Native> res;
            res.first = key;
            ExtractOffset(res.first, res.second, index);
            res.second = -res.second; // different convention
            return res;
        }

        Block::SystemState::ID GetEmptyID()
        {
            Block::SystemState::ID id;
            ZeroObject(id);
            id.m_Height = Rules::HeightGenesis;
            return id;
        }
    }

    Wallet::Wallet(IWalletDB::Ptr walletDB, TxCompletedAction&& action)
        : m_WalletDB{ walletDB }
        , m_pNodeNetwork(nullptr)
        , m_pWalletNetwork(nullptr)
        , m_tx_completed_action{move(action)}
        , m_LastSyncTotal(0)
        , m_needRecover{false}
        , m_recovering{false}
    {
        assert(walletDB);
        resume_all_tx();
    }

	void Wallet::get_Kdf(Key::IKdf::Ptr& pKdf)
	{
		pKdf = m_WalletDB->get_MasterKdf();
	}

	void Wallet::OnOwnedNode(const PeerID& id, bool bUp)
	{
		// TODO
	}

    Block::SystemState::IHistory& Wallet::get_History()
    {
        return m_WalletDB->get_History();
    }

    void Wallet::set_Network(proto::FlyClient::INetwork& netNode, INetwork& netWallet)
    {
        m_pNodeNetwork = &netNode;
        m_pWalletNetwork = &netWallet;
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

    TxID Wallet::transfer_money(const WalletID& from, const WalletID& to, Amount amount, Amount fee, bool sender, ByteBuffer&& message)
    {
        auto txID = wallet::GenerateTxID();
        auto tx = constructTransaction(txID, TxType::Simple);

        Height currentHeight = m_WalletDB->getCurrentHeight();
        tx->SetParameter(TxParameterID::TransactionType, TxType::Simple);
        tx->SetParameter(TxParameterID::CreateTime, getTimestamp());
        tx->SetParameter(TxParameterID::Amount, amount);
        tx->SetParameter(TxParameterID::Fee, fee);
        tx->SetParameter(TxParameterID::MinHeight, currentHeight);
        tx->SetParameter(TxParameterID::MaxHeight, currentHeight + 1440); // transaction is valid +24h from now
        tx->SetParameter(TxParameterID::PeerID, to);
        tx->SetParameter(TxParameterID::MyID, from);
        tx->SetParameter(TxParameterID::Message, move(message));
        tx->SetParameter(TxParameterID::IsSender, sender);
        tx->SetParameter(TxParameterID::IsInitiator, true);
        tx->SetParameter(TxParameterID::Status, TxStatus::Pending);

        m_transactions.emplace(txID, tx);

        updateTransaction(txID);

        return txID;
    }

    TxID Wallet::swap_coins(const WalletID& from, const WalletID& to, Amount amount, Amount fee, wallet::AtomicSwapCoin swapCoin, Amount swapAmount)
    {
        auto txID = wallet::GenerateTxID();
        auto tx = constructTransaction(txID, TxType::AtomicSwap);

        tx->SetParameter(TxParameterID::TransactionType, TxType::AtomicSwap);
        tx->SetParameter(TxParameterID::CreateTime, getTimestamp());
        tx->SetParameter(TxParameterID::Amount, amount);
        tx->SetParameter(TxParameterID::Fee, fee);
        tx->SetParameter(TxParameterID::MinHeight, m_WalletDB->getCurrentHeight());
        tx->SetParameter(TxParameterID::PeerID, to);
        tx->SetParameter(TxParameterID::MyID, from);
        tx->SetParameter(TxParameterID::IsSender, true);
        tx->SetParameter(TxParameterID::IsInitiator, true);
        tx->SetParameter(TxParameterID::Status, TxStatus::Pending);

        tx->SetParameter(TxParameterID::AtomicSwapCoin, swapCoin);
        tx->SetParameter(TxParameterID::AtomicSwapAmount, swapAmount);

        m_transactions.emplace(txID, tx);

        updateTransaction(txID);

        return txID;
    }

    void Wallet::recover()
    {
        LOG_INFO() << "Recover coins from blockchain";
        m_WalletDB->clear();
        m_needRecover = true;
    }

    void Wallet::resume_tx(const TxDescription& tx)
    {
        if (tx.canResume() && m_transactions.find(tx.m_txId) == m_transactions.end())
        {
            auto t = constructTransaction(tx.m_txId, TxType::Simple);

            m_transactions.emplace(tx.m_txId, t);
        }
    }

    void Wallet::resume_all_tx()
    {
        auto txs = m_WalletDB->getTxHistory();
        for (auto& tx : txs)
        {
            resume_tx(tx);
        }
    }

    void Wallet::on_tx_completed(const TxID& txID)
    {
        auto it = m_transactions.find(txID);
        if (it != m_transactions.end())
        {
            m_transactions.erase(it);
        }
 
        if (m_tx_completed_action)
        {
            m_tx_completed_action(txID);
        }
    }

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

    bool Wallet::MyRequestTransaction::operator < (const MyRequestTransaction& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestMined::operator < (const MyRequestMined& x) const
    {
        return false;
    }

	bool Wallet::MyRequestUtxoEvents::operator < (const MyRequestUtxoEvents& x) const
	{
		return false;
	}

	bool Wallet::MyRequestRecover::operator < (const MyRequestRecover& x) const
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

    void Wallet::confirm_kernel(const TxID& txID, const TxKernel& kernel)
    {
        if (auto it = m_transactions.find(txID); it != m_transactions.end())
        {
            MyRequestKernel::Ptr pVal(new MyRequestKernel);
            pVal->m_TxID = txID;
            kernel.get_ID(pVal->m_Msg.m_ID);

            if (PostReqUnique(*pVal))
                LOG_INFO() << "Get proof for kernel: " << pVal->m_Msg.m_ID;
        }
    }

    bool Wallet::get_tip(Block::SystemState::Full& state) const
    {
        return m_WalletDB->get_History().get_Tip(state);
    }

    void Wallet::send_tx_params(const WalletID& peerID, SetTxParameter&& msg)
    {
        m_pWalletNetwork->Send(peerID, std::move(msg));
    }

    void Wallet::OnWalletMsg(const WalletID& myID, wallet::SetTxParameter&& msg)
    {
        auto t = getTransaction(myID, msg);
        if (!t)
        {
            return;
        }
        bool txChanged = false;
        for (const auto& p : msg.m_Parameters)
        {
            if (p.first < TxParameterID::PrivateFirstParam)
            {
                txChanged |= t->SetParameter(p.first, p.second);
            }
            else
            {
                LOG_WARNING() << "Attempt to set private tx parameter";
            }
        }
        if (txChanged)
        {
            updateTransaction(msg.m_txId);
        }
    }

    void Wallet::OnRequestComplete(MyRequestTransaction& r)
    {
        LOG_DEBUG() << r.m_TxID << (r.m_Res.m_Value ? " has registered" : " has failed to register");
        
        auto it = m_transactions.find(r.m_TxID);
        if (it != m_transactions.end())
        {
            it->second->SetParameter(TxParameterID::TransactionRegistered, r.m_Res.m_Value);
            updateTransaction(r.m_TxID);
        }
    }

    void Wallet::cancel_tx(const TxID& txId)
    {
        LOG_INFO() << "Canceling tx " << txId;

        if (auto it = m_transactions.find(txId); it != m_transactions.end())
        {
            it->second->Cancel();
        }
        else
        {
            m_WalletDB->deleteTx(txId);
        }
    }

    void Wallet::delete_tx(const TxID& txId)
    {
        LOG_INFO() << "deleting tx " << txId;
        if (auto it = m_transactions.find(txId); it == m_transactions.end())
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
        auto it = m_transactions.find(txID);
        if (it != m_transactions.end())
        {
            bool bSynced = !SyncRemains();

            if (bSynced)
                it->second->Update();
            else
                m_TransactionsToUpdate.insert(it->second);
        }
        else
        {
            LOG_DEBUG() << txID << " Unexpected event";
        }
    }

    void Wallet::OnRequestComplete(MyRequestUtxo& r)
    {
        // TODO: handle the maturity of the several proofs (> 1)
        if (r.m_Res.m_Proofs.empty())
        {
            LOG_WARNING() << "Got empty utxo proof for: " << r.m_Msg.m_Utxo;

            if (r.m_Coin.m_status == Coin::Locked)
            {
                r.m_Coin.m_status = Coin::Spent;
                m_WalletDB->update(r.m_Coin);
                assert(r.m_Coin.m_spentTxId.is_initialized());
                updateTransaction(*(r.m_Coin.m_spentTxId));
            }
            else if (r.m_Coin.m_status == Coin::Unconfirmed && r.m_Coin.isReward())
            {
                LOG_WARNING() << "Uncofirmed reward UTXO removed. Amount: " << r.m_Coin.m_amount << " Height: " << r.m_Coin.m_createHeight;
                m_WalletDB->remove(r.m_Coin);
            }

            return;
        }

        for (const auto& proof : r.m_Res.m_Proofs)
        {
            if (r.m_Coin.m_status == Coin::Unconfirmed)
            {
                Block::SystemState::Full sTip;
                get_tip(sTip);

                LOG_INFO() << "Got utxo proof for: " << r.m_Msg.m_Utxo;
                r.m_Coin.m_status = Coin::Unspent;
                r.m_Coin.m_maturity = proof.m_State.m_Maturity;
                r.m_Coin.m_confirmHeight = sTip.m_Height;
                sTip.get_Hash(r.m_Coin.m_confirmHash);
                if (r.m_Coin.m_id == 0)
                {
                    m_WalletDB->store(r.m_Coin);
                }
                else
                {
                    m_WalletDB->update(r.m_Coin);
                }
                if (r.m_Coin.isReward())
                {
                    LOG_INFO() << "Block reward received: " << PrintableAmount(r.m_Coin.m_amount);
                }
                else if (r.m_Coin.m_createTxId.is_initialized())
                {
                    updateTransaction(*(r.m_Coin.m_createTxId));
                }
            }
        }
    }

    void Wallet::OnRequestComplete(MyRequestMined& r)
    {
        auto currentHeight = m_WalletDB->getCurrentHeight();
        Height lastKnownCoinHeight = currentHeight;
        for (auto& minedCoin : r.m_Res.m_Entries)
        {
            if (minedCoin.m_Active && minedCoin.m_ID.m_Height >= currentHeight) // we store coins from active branch
            {
                // coinbase 
                Coin c(Rules::get().CoinbaseEmission
                    , Coin::Unconfirmed
                    , minedCoin.m_ID.m_Height
                    , MaxHeight
                    , Key::Type::Coinbase);

                getUtxoProof(c);

                if (minedCoin.m_Fees > 0)
                {
                    Coin cFee(minedCoin.m_Fees
                        , Coin::Unconfirmed
                        , minedCoin.m_ID.m_Height
                        , MaxHeight
                        , Key::Type::Comission);

                    getUtxoProof(cFee);
                }
                lastKnownCoinHeight = max(lastKnownCoinHeight, minedCoin.m_ID.m_Height);
            }
        }

        if (r.m_Res.m_Entries.size() == proto::PerMined::s_EntriesMax)
        {
            r.m_Msg.m_HeightMin = lastKnownCoinHeight;
            PostReqUnique(r);
        }
    }

    void Wallet::OnRequestComplete(MyRequestRecover& r)
    {
        for (const auto& kidv : r.m_Res.m_Private)
            getUtxoProof(Coin::fromKidv(kidv));
    }

    void Wallet::OnRequestComplete(MyRequestKernel& r)
    {
        if (!r.m_Res.m_Proof.empty())
        {
            m_WalletDB->get_History().AddStates(&r.m_Res.m_Proof.m_State, 1); // why not?

            auto it = m_transactions.find(r.m_TxID);
            if (m_transactions.end() != it)
            {
                if (it->second->SetParameter(TxParameterID::KernelProofHeight, r.m_Res.m_Proof.m_State.m_Height))
                    it->second->Update();
            }
        }
    }

    void Wallet::OnRequestComplete(MyRequestBbsMsg& r)
    {
        assert(false);
    }

	void Wallet::OnRequestComplete(MyRequestUtxoEvents& r)
	{
	}

    void Wallet::OnRolledBack()
    {
        Block::SystemState::Full sTip;
        m_WalletDB->get_History().get_Tip(sTip);

        m_WalletDB->get_History().DeleteFrom(sTip.m_Height + 1);

        m_WalletDB->rollbackConfirmedUtxo(sTip.m_Height);

        for (auto it = m_transactions.begin(); m_transactions.end() != it; it++)
        {
            const auto& pTx = it->second;

            Height h;
            if (pTx->GetParameter(TxParameterID::KernelProofHeight, h) && (h > sTip.m_Height))
            {
                h = 0;
                pTx->SetParameter(TxParameterID::KernelProofHeight, h);
                m_TransactionsToUpdate.insert(pTx);
            }
        }
    }

    void Wallet::OnNewTip()
    {
        if (m_recovering)
        {
            // ignore when recover is in progress
            return;
        }

        m_WalletDB->ShrinkHistory();

        Block::SystemState::Full sTip;
        get_tip(sTip);
        if (!sTip.m_Height)
            return; //?!

        Block::SystemState::ID id, id2;
        sTip.get_ID(id);
        LOG_INFO() << "Sync up to " << id;

        if (!m_WalletDB->getSystemStateID(id2))
            id2.m_Height = 0;

        if (m_needRecover)
        {
            MyRequestRecover::Ptr pReq(new MyRequestRecover);
            pReq->m_Msg.m_Private = true;
            pReq->m_Msg.m_Public = true;
            PostReqUnique(*pReq);
            return;
        }

        {
            MyRequestMined::Ptr pReq(new MyRequestMined);
            pReq->m_Msg.m_HeightMin = id2.m_Height;
            PostReqUnique(*pReq);
        }

        auto t = m_transactions;
        for (auto& p : t)
        {
            p.second->Update();
        }

        // try to restore utxo state after reset, rollback and etc..
        uint32_t nUnconfirmed = 0;
        m_WalletDB->visit([&nUnconfirmed, this](const Coin& c)->bool
        {
            if (c.m_status == Coin::Unconfirmed
                && ((c.m_createTxId.is_initialized()
                && (m_transactions.find(*c.m_createTxId) == m_transactions.end())) || c.isReward()))
            {
                getUtxoProof(c);
                nUnconfirmed++;
            }
            return true;
        });

        if (nUnconfirmed)
        {
            LOG_INFO() << "Found " << nUnconfirmed << " unconfirmed utxo to proof";
        }

        CheckSyncDone();
    }

    void Wallet::getUtxoProof(const Coin& coin)
    {
        MyRequestUtxo::Ptr pReq(new MyRequestUtxo);
        pReq->m_Coin = coin;
        pReq->m_Msg.m_Utxo = Commitment(m_WalletDB->calcKey(coin), coin.m_amount);
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
        m_recovering = false;
        notifySyncProgress();

        std::set<wallet::BaseTransaction::Ptr> txSet;
        txSet.swap(m_TransactionsToUpdate);

        for (auto it = txSet.begin(); txSet.end() != it; it++)
        {
            const wallet::BaseTransaction::Ptr& pTx = *it;
            if (m_transactions.find(pTx->GetTxID()) != m_transactions.end())
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

    void Wallet::register_tx(const TxID& txId, Transaction::Ptr data)
    {
        LOG_VERBOSE() << txId << " sending tx for registration";

#ifndef NDEBUG
        TxBase::Context ctx;
        assert(data->IsValid(ctx));
#endif // NDEBUG

        MyRequestTransaction::Ptr pReq(new MyRequestTransaction);
        pReq->m_TxID = txId;
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
        auto it = m_transactions.find(msg.m_txId);
        if (it != m_transactions.end())
        {
            if (it->second->GetType() != msg.m_Type)
            {
                LOG_WARNING() << msg.m_txId << " Parameters for invalid tx type";
            }
            return it->second;
        }

        TxType type = TxType::Simple;
        if (wallet::getTxParameter(m_WalletDB, msg.m_txId, TxParameterID::TransactionType, type))
        {
            // we return only active transactions
            return BaseTransaction::Ptr();
        }

        auto t = constructTransaction(msg.m_txId, msg.m_Type);

        t->SetParameter(TxParameterID::TransactionType, msg.m_Type);
        t->SetParameter(TxParameterID::CreateTime, getTimestamp());
        t->SetParameter(TxParameterID::MyID, myID);
        t->SetParameter(TxParameterID::PeerID, msg.m_from);
        t->SetParameter(TxParameterID::IsInitiator, false);
        t->SetParameter(TxParameterID::Status, TxStatus::Pending);

        auto address = m_WalletDB->getAddress(myID);
        if (address.is_initialized())
        {
            ByteBuffer message(address->m_label.begin(), address->m_label.end());
            t->SetParameter(TxParameterID::Message, message);
        }

        m_transactions.emplace(msg.m_txId, t);
        return t;
    }

    wallet::BaseTransaction::Ptr Wallet::constructTransaction(const TxID& id, TxType type)
    {
        switch (type)
        {
        case TxType::Simple:
             return make_shared<SimpleTransaction>(*this, m_WalletDB, id);
        case TxType::AtomicSwap:
            return make_shared<AtomicSwapTransaction>(*this, m_WalletDB, id);
        }
        return wallet::BaseTransaction::Ptr();
    }
}
