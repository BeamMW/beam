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

    struct Wallet::StateFinder
    {
        StateFinder(Height newHeight, IKeyChain::Ptr keychain)
            : m_first{ 0 }
            , m_syncHeight{newHeight}
            , m_count{ int64_t(keychain->getKnownStateCount()) }
            , m_step{0}
            , m_id{wallet::GetEmptyID()}
            , m_keychain{keychain}
        {
        }

        Height getSearchHeight()
        {
            auto id = m_keychain->getKnownStateID(getSearchOffset());
            assert(id.m_Height >= Rules::HeightGenesis);
            return id.m_Height;
        }

        Height getSearchOffset()
        {
            m_step = (m_count >> 1);
            return m_first + m_step;
        }

        void moveBack()
        {
            m_count = m_step;
        }

        void moveForward()
        {
            m_first += m_step + 1;
            m_count -= m_step + 1;
        }

        Height m_first;
        Height m_syncHeight;
        int64_t m_count;
        int64_t m_step;
        Block::SystemState::ID m_id;
        IKeyChain::Ptr m_keychain;
    };


    Wallet::Wallet(IKeyChain::Ptr keyChain, INetworkIO::Ptr network, bool holdNodeConnection, TxCompletedAction&& action)
        : m_keyChain{ keyChain }
        , m_network{ network }
        , m_tx_completed_action{move(action)}
        , m_newState{}
        , m_knownStateID{wallet::GetEmptyID()}
        , m_syncDone{0}
        , m_syncTotal{0}
        , m_synchronized{false}
        , m_holdNodeConnection{ holdNodeConnection }
        , m_needRecover{false}
    {
        assert(keyChain);
        ZeroObject(m_newState);
        m_keyChain->getSystemStateID(m_knownStateID);
        m_network->set_wallet(this);
        resume_all_tx();
    }

    Wallet::~Wallet()
    {
        m_network->set_wallet(nullptr);
        assert(m_reg_requests.empty());
    }

    TxID Wallet::transfer_money(const WalletID& from, const WalletID& to, Amount amount, Amount fee, bool sender, ByteBuffer&& message)
    {
        auto txID = wallet::GenerateTxID();
        auto tx = constructTransaction(txID, TxType::Simple);

        tx->SetParameter(TxParameterID::TransactionType, TxType::Simple);
        tx->SetParameter(TxParameterID::CreateTime, getTimestamp());
        tx->SetParameter(TxParameterID::Amount, amount);
        tx->SetParameter(TxParameterID::Fee, fee);
        tx->SetParameter(TxParameterID::MinHeight, m_keyChain->getCurrentHeight());
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
        tx->SetParameter(TxParameterID::MinHeight, m_keyChain->getCurrentHeight());
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
        m_keyChain->clear();
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
        auto txs = m_keyChain->getTxHistory();
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
        if (m_syncDone == m_syncTotal)
        {
            close_node_connection();
        }
    }

    void Wallet::confirm_outputs(const vector<Coin>& coins)
    {
        getUtxoProofs(coins);
    }

    void Wallet::confirm_kernel(const TxID& txID, const TxKernel& kernel)
    {
        if (auto it = m_transactions.find(txID); it != m_transactions.end())
        {
            proto::GetProofKernel kernelMsg = {};
            kernel.get_ID(kernelMsg.m_ID);
            LOG_INFO() << "Get proof for kernel: " << kernelMsg.m_ID;
            m_pendingKernelProofs.push_back(it->second);
            enter_sync();
            m_network->send_node_message(move(kernelMsg));
        }
    }

    bool Wallet::get_tip(Block::SystemState::Full& state) const
    {
        if (m_newState.IsValid())
        {
            state = m_newState;
            return true;
        }
        LOG_ERROR() << "get_tip: Invalid state " ;
        return false;
    }

    bool Wallet::isTestMode() const
    {
        return IsTestMode();
    }

    void Wallet::send_tx_params(const WalletID& peerID, SetTxParameter&& msg)
    {
        m_network->send_tx_message(peerID, std::move(msg));
    }

    void Wallet::handle_tx_message(const WalletID& myID, wallet::SetTxParameter&& msg)
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

    bool Wallet::handle_node_message(proto::Boolean&& res)
    {
        if (m_reg_requests.empty())
        {
            LOG_DEBUG() << "Received unexpected tx registration confirmation";
            assert(m_transactions.empty());
        }
        else
        {
            auto txId = m_reg_requests.front().first;
            m_reg_requests.pop_front();
            handle_tx_registered(txId, res.m_Value);
        }
        return close_node_connection();
    }

    void Wallet::handle_tx_registered(const TxID& txId, bool res)
    {
        LOG_DEBUG() << txId << (res ? " has registered" : " has failed to register");
        
        auto it = m_transactions.find(txId);
        if (it != m_transactions.end())
        {
            it->second->SetParameter(TxParameterID::TransactionRegistered, res);
            updateTransaction(txId);
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
            m_keyChain->deleteTx(txId);
        }
    }

    void Wallet::delete_tx(const TxID& txId)
    {
        LOG_INFO() << "deleting tx " << txId;
        if (auto it = m_transactions.find(txId); it == m_transactions.end())
        {
            m_keyChain->deleteTx(txId);
        }
        else
        {
            LOG_WARNING() << "Cannot delete running transaction";
        }
    }

    void Wallet::set_node_address(io::Address node_address)
    {
        m_network->set_node_address(node_address);
    }

    void Wallet::resetSystemState()
    {
        ZeroObject(m_newState);
        ZeroObject(m_knownStateID);
        m_keyChain->setSystemStateID(m_knownStateID);
        m_keyChain->rollbackConfirmedUtxo(0);
    }

    void Wallet::updateTransaction(const TxID& txID)
    {
        auto f = [&]()
        {
            for (auto t : m_TransactionsToUpdate)
            {
                t->Update();
            }
            m_TransactionsToUpdate.clear();
        };

        auto it = m_transactions.find(txID);
        if (it != m_transactions.end())
        {
            m_TransactionsToUpdate.insert(it->second);
            if (m_synchronized)
            {
                f();
            }
            else
            {
                m_pendingEvents.emplace_back(std::move(f));
            }
        }
        else
        {
            LOG_DEBUG() << txID << " Unexpected event";
        }
    }

    bool Wallet::get_IdentityKeyForNode(ECC::Scalar::Native& sk, const PeerID& idNode)
    {
        // TODO: Report your identity *only* to the owned nodes, otherwise it's very demasking!
        m_keyChain->get_IdentityKey(sk);
        return true;
    }

    bool Wallet::handle_node_message(proto::ProofUtxo&& utxoProof)
    {
        // TODO: handle the maturity of the several proofs (> 1)
        if (m_pendingUtxoProofs.empty())
        {
            LOG_WARNING() << "Unexpected UTXO proof";
            return exit_sync();
        }

        Coin& coin = m_pendingUtxoProofs.front();
        Input input;
        input.m_Commitment = Commitment(m_keyChain->calcKey(coin), coin.m_amount);
        if (utxoProof.m_Proofs.empty())
        {
            LOG_WARNING() << "Got empty utxo proof for: " << input.m_Commitment;

            if (coin.m_status == Coin::Locked)
            {
                coin.m_status = Coin::Spent;
                m_keyChain->update(coin);
                assert(coin.m_spentTxId.is_initialized());
                updateTransaction(*(coin.m_spentTxId));
            }
            else if (coin.m_status == Coin::Unconfirmed && coin.isReward())
            {
                LOG_WARNING() << "Uncofirmed reward UTXO removed. Amount: " << coin.m_amount << " Height: " << coin.m_createHeight;
                m_keyChain->remove(coin);
            }
        }
        else
        {
            for (const auto& proof : utxoProof.m_Proofs)
            {
                if (coin.m_status == Coin::Unconfirmed)
                {
                    if (IsTestMode() || m_newState.IsValidProofUtxo(input, proof))
                    {
                        LOG_INFO() << "Got utxo proof for: " << input.m_Commitment;
                        coin.m_status = Coin::Unspent;
                        coin.m_maturity = proof.m_State.m_Maturity;
                        coin.m_confirmHeight = m_newState.m_Height;
                        m_newState.get_Hash(coin.m_confirmHash);
                        if (coin.m_id == 0)
                        {
                            m_keyChain->store(coin);
                        }
                        else
                        {
                            m_keyChain->update(coin);
                        }
                        if (coin.isReward())
                        {
                            LOG_INFO() << "Block reward received: " << PrintableAmount(coin.m_amount);
                        }
                        else if (coin.m_createTxId.is_initialized())
                        {
                            updateTransaction(*(coin.m_createTxId));
                        }
                    }
                    else
                    {
                        LOG_ERROR() << "Invalid utxo proof provided: " << input.m_Commitment;
                    }
                }
            }
        }

        m_pendingUtxoProofs.pop_front();
        m_PendingUtxoUnique.erase(input.m_Commitment);
        assert(m_pendingUtxoProofs.size() == m_PendingUtxoUnique.size());

        return exit_sync();
    }

    bool Wallet::handle_node_message(proto::NewTip&& msg)
    {
        m_pending_reg_requests.clear();

        Block::SystemState::ID newID;
        msg.m_Description.get_ID(newID);

        m_newState = msg.m_Description;

        if (newID == m_knownStateID)
        {
            // here we may close connection with node
            saveKnownState();
            return close_node_connection();
        }

        if (m_knownStateID.m_Height <= Rules::HeightGenesis && !m_stateFinder)
        {
            // cold start
            do_fast_forward();
            return true;
        }
        else
        {
            enter_sync();
        }

        m_network->send_node_message(proto::GetProofState{ m_knownStateID.m_Height });

        return true;
    }

    bool Wallet::handle_node_message(proto::Mined&& msg)
    {
        vector<Coin> mined;
        auto currentHeight = m_keyChain->getCurrentHeight();
        Height lastKnownCoinHeight = currentHeight;
        for (auto& minedCoin : msg.m_Entries)
        {
            if (minedCoin.m_Active && minedCoin.m_ID.m_Height >= currentHeight) // we store coins from active branch
            {
                // coinbase 
                mined.emplace_back(Rules::get().CoinbaseEmission
                                 , Coin::Unconfirmed
                                 , minedCoin.m_ID.m_Height
                                 , MaxHeight
                                 , Key::Type::Coinbase);
                if (minedCoin.m_Fees > 0)
                {
                    mined.emplace_back(minedCoin.m_Fees
                                     , Coin::Unconfirmed
                                     , minedCoin.m_ID.m_Height
                                     , MaxHeight
                                     , Key::Type::Comission);
                }
                lastKnownCoinHeight = minedCoin.m_ID.m_Height;
            }
        }

        if (!mined.empty())
        {
            getUtxoProofs(mined);
        }

        if (msg.m_Entries.size() == proto::PerMined::s_EntriesMax)
        {
            enter_sync();
            m_network->send_node_message(proto::GetMined{ lastKnownCoinHeight });
        }

        return exit_sync();
    }

    bool Wallet::handle_node_message(proto::Recovered&& msg)
    {
        vector<Coin> coins;
        coins.reserve(msg.m_Private.size());
        for (const auto& kidv : msg.m_Private)
        {
            coins.push_back(Coin::fromKidv(kidv));
        }
        
        getUtxoProofs(coins);
        return exit_sync();
    }

    bool Wallet::handle_node_message(proto::ProofState&& msg)
    {
        if (!IsTestMode() 
            && m_knownStateID.m_Height > Rules::HeightGenesis 
            && !m_newState.IsValidProofState(m_knownStateID, msg.m_Proof) )
        {
            // rollback
            // search for the latest valid known state
            if (!m_stateFinder || m_stateFinder->m_syncHeight < m_newState.m_Height)
            {
                // restart search
                if (!m_stateFinder)
                {
                    LOG_INFO() << "State " << m_knownStateID << " doesn't present on current branch. Rollback... ";
                }
                else
                {
                    LOG_INFO() << "Restarting rollback...";
                }
                m_stateFinder.reset(new StateFinder(m_newState.m_Height, m_keyChain));
                enter_sync();
                m_network->send_node_message(proto::GetProofState{ m_stateFinder->getSearchHeight() });
                return exit_sync();
            }
            auto id = m_keyChain->getKnownStateID(m_stateFinder->getSearchOffset());
            LOG_INFO() << "Check state: " << id;

            if (m_newState.IsValidProofState(id, msg.m_Proof))
            {
                m_stateFinder->m_id = id;
                m_stateFinder->moveForward();
            }
            else
            {
                m_stateFinder->moveBack();
            }

            if (m_stateFinder->m_count > 0)
            {
                enter_sync();
                m_network->send_node_message(proto::GetProofState{ m_stateFinder->getSearchHeight() });
                return exit_sync();
            }
            else
            {
                if (m_stateFinder->m_id.m_Height != MaxHeight)
                {
                    m_keyChain->rollbackConfirmedUtxo(m_stateFinder->m_id.m_Height);
                    m_knownStateID = m_stateFinder->m_id;
                }
                else
                {
                    m_knownStateID = wallet::GetEmptyID();
                }
                m_stateFinder.reset();
                LOG_INFO() << "Rolled back to " << m_knownStateID;
            }
        }

        do_fast_forward();

        return exit_sync();
    }

    bool Wallet::handle_node_message(proto::ProofKernel&& msg)
    {
        if (m_pendingKernelProofs.empty())
        {
            LOG_WARNING() << "Unexpected Kernel proof";
            return exit_sync();
        }
        auto tx = m_pendingKernelProofs.front();
        m_pendingKernelProofs.pop_front();

        if (!msg.m_Proof.empty() || IsTestMode())
        {
            if (tx->SetParameter(TxParameterID::KernelProof, msg.m_Proof))
            {
                tx->Update();
            }
        }

        return exit_sync();
    }

    void Wallet::abort_sync()
    {
        m_syncDone = m_syncTotal = 0;
        copy(m_reg_requests.begin(), m_reg_requests.end(), back_inserter(m_pending_reg_requests));
        m_reg_requests.clear();
        m_pendingUtxoProofs.clear();
        m_PendingUtxoUnique.clear();

        notifySyncProgress();
    }

    void Wallet::do_fast_forward()
    {
        Block::SystemState::ID id;
        m_newState.get_ID(id);
        LOG_INFO() << "Sync up to " << id;
        // fast-forward
        enter_sync(); // Mined 
        if (m_needRecover)
        {
            m_network->send_node_message(proto::Recover{ true, true });
            return;
        }
        else
        {
            m_network->send_node_message(proto::GetMined{ m_knownStateID.m_Height });
        }

        auto t = m_transactions;
        for (auto& p : t)
        {
            p.second->Update();
        }

        // try to restore utxo state after reset, rollback and etc..
        vector<Coin> unconfirmedUtxo;
        m_keyChain->visit([&unconfirmedUtxo, this](const Coin& c)->bool
        {
            if (c.m_status == Coin::Unconfirmed
                && ((c.m_createTxId.is_initialized()
                && (m_transactions.find(*c.m_createTxId) == m_transactions.end())) || c.isReward()))
            {
                unconfirmedUtxo.push_back(c);
            }
            return true;
        });

        if (!unconfirmedUtxo.empty())
        {
            LOG_INFO() << "Found " << unconfirmedUtxo.size() << " unconfirmed utxo to proof";
            getUtxoProofs(unconfirmedUtxo);
        }
    }

    void Wallet::getUtxoProofs(const vector<Coin>& coins)
    {
        for (auto& coin : coins)
        {
            Input input;
            input.m_Commitment = Commitment(m_keyChain->calcKey(coin), coin.m_amount);
            if (m_PendingUtxoUnique.find(input.m_Commitment) != m_PendingUtxoUnique.end())
            {
                continue;
            }

            enter_sync();
            m_pendingUtxoProofs.push_back(coin);
            m_PendingUtxoUnique.insert(input.m_Commitment);
            assert(m_pendingUtxoProofs.size() == m_PendingUtxoUnique.size());
            LOG_DEBUG() << "Get utxo proof: " << input.m_Commitment;
            m_network->send_node_message(proto::GetProofUtxo{ input, 0 });
        }
    }

    void Wallet::enter_sync()
    {
        if (m_syncTotal == 0)
        {
            m_synchronized = false;
        }
        ++m_syncTotal;
        report_sync_progress();
        
    }

    bool Wallet::exit_sync()
    {
        if (m_syncTotal)
        {
            ++m_syncDone;
            report_sync_progress();
            assert(m_syncDone <= m_syncTotal);
            if (m_syncDone == m_syncTotal)
            {
                m_newState.get_ID(m_knownStateID);
                saveKnownState();
            }
        }

        return close_node_connection();
    }

    void Wallet::saveKnownState()
    {
        m_keyChain->setSystemStateID(m_knownStateID);
        LOG_INFO() << "Current state is " << m_knownStateID;
        m_synchronized = true;
        m_needRecover = false;
        m_syncDone = m_syncTotal = 0;
        notifySyncProgress();
        if (!m_pendingEvents.empty())
        {
            for (auto& cb : m_pendingEvents)
            {
                cb();
            }
            m_pendingEvents.clear();
        }
    }

    void Wallet::notifySyncProgress()
    {
        for (auto sub : m_subscribers)
        {
            sub->onSyncProgress(m_syncDone, m_syncTotal);
        }
    }

    void Wallet::report_sync_progress()
    {
        assert(m_syncDone <= m_syncTotal);
        int p = static_cast<int>((m_syncDone * 100) / m_syncTotal);
        LOG_INFO() << "Synchronizing with node: " << p << "% (" << m_syncDone << "/" << m_syncTotal << ")";

        notifySyncProgress();
    }

    bool Wallet::close_node_connection()
    {
        if (m_synchronized && m_transactions.empty())
        {
            notifySyncProgress();
            if (!m_holdNodeConnection)
            {
                m_network->close_node_connection();
            }
        }
        return true;
    }

    void Wallet::register_tx(const TxID& txId, Transaction::Ptr data)
    {
        LOG_VERBOSE() << txId << " sending tx for registration";
        TxBase::Context ctx;
        assert(data->IsValid(ctx));
        m_reg_requests.push_back(make_pair(txId, data));
        m_network->send_node_message(proto::NewTransaction{ data, false });
    }

    void Wallet::subscribe(IWalletObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());

        m_subscribers.push_back(observer);

        m_keyChain->subscribe(observer);
    }

    void Wallet::unsubscribe(IWalletObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);

        assert(it != m_subscribers.end());

        m_subscribers.erase(it);

        m_keyChain->unsubscribe(observer);
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
        if (wallet::getTxParameter(m_keyChain, msg.m_txId, TxParameterID::TransactionType, type))
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

        auto address = m_keyChain->getAddress(myID);
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
                return make_shared<SimpleTransaction>(*this, m_keyChain, id);
        case TxType::AtomicSwap:
            return make_shared<AtomicSwapTransaction>(*this, m_keyChain, id);
        }
        return wallet::BaseTransaction::Ptr();
    }
}
