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
#include <boost/uuid/uuid_generators.hpp>
#include "core/ecc_native.h"
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "utility/helpers.h"
#include <algorithm>
#include <random>
#include <iomanip>

namespace
{
    const char* ReceiverPrefix = "[Receiver] ";
    const char* SenderPrefix = "[Sender] ";
}

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
        const string_view chattles{ " mils " };
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
    }

    struct Wallet::StateFinder
    {
        StateFinder(Height newHeight, IKeyChain::Ptr keychain)
            : m_first{ 0 }
            , m_syncHeight{newHeight}
            , m_count{ int64_t(keychain->getKnownStateCount()) }
            , m_step{0}
            , m_id{}
            , m_keychain{keychain}
        {

        }

        Height getSearchHeight()
        {
            auto id = m_keychain->getKnownStateID(getSearchOffset());
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
        , m_knownStateID{}
        , m_syncDone{0}
        , m_syncTotal{0}
        , m_synchronized{false}
        , m_holdNodeConnection{ holdNodeConnection }
    {
        assert(keyChain);
        m_keyChain->getSystemStateID(m_knownStateID);
        m_network->set_wallet(this);
        resume_all_tx();
    }

    Wallet::~Wallet()
    {
        m_network->set_wallet(nullptr);
        assert(m_reg_requests.empty());
        assert(m_removedNegotiators.empty());
    }

    TxID Wallet::transfer_money(const WalletID& from, const WalletID& to, Amount amount, Amount fee, bool sender, ByteBuffer&& message)
    {
        boost::uuids::uuid id = boost::uuids::random_generator()();
        TxID txId{};
        copy(id.begin(), id.end(), txId.begin());
        TxDescription tx( txId, amount, fee, m_keyChain->getCurrentHeight(), to, from, move(message), getTimestamp(), sender);
        m_keyChain->saveTx(tx);
        resume_negotiator(tx);
        return txId;
    }

    void Wallet::resume_tx(const TxDescription& tx)
    {
        if (tx.canResume() && m_negotiators.find(tx.m_txId) == m_negotiators.end())
        {
            Cleaner c{ m_removedNegotiators };
            auto s = make_shared<Negotiator>(*this, m_keyChain, tx);

            m_negotiators.emplace(tx.m_txId, s);
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

    void Wallet::send_tx_invitation(const TxDescription& tx, Invite&& data)
    {
        send_tx_message(tx, move(data));
    }

    void Wallet::send_tx_confirmation(const TxDescription& tx, ConfirmTransaction&& data)
    {
        send_tx_message(tx, move(data));
    }

    void Wallet::on_tx_completed(const TxDescription& tx)
    {
        auto it = m_negotiators.find(tx.m_txId);
        if (it != m_negotiators.end())
        {
            m_removedNegotiators.push_back(move(it->second));
            m_negotiators.erase(it);
        }
 
        if (m_tx_completed_action)
        {
            m_tx_completed_action(tx.m_txId);
        }
        if (m_syncDone == m_syncTotal)
        {
            close_node_connection();
        }
    }


    void Wallet::send_tx_failed(const TxDescription& tx)
    {
        send_tx_message(tx, wallet::TxFailed{ tx.m_peerId, tx.m_txId });
    }

    void Wallet::send_tx_confirmation(const TxDescription& tx, ConfirmInvitation&& data)
    {
        send_tx_message(tx, move(data));
    }

    void Wallet::register_tx(const TxDescription& tx, Transaction::Ptr data)
    {
        register_tx(tx.m_txId, data);
    }

    void Wallet::send_tx_registered(const TxDescription& tx)
    {
        send_tx_message(tx, wallet::TxRegistered{ tx.m_peerId, tx.m_txId, true });
    }

    void Wallet::confirm_outputs(const TxDescription& tx)
    {
        if (auto it = m_negotiators.find(tx.m_txId); it != m_negotiators.end())
        {
            get_kernel_proof(it->second);
        }
    }

    void Wallet::handle_tx_message(const WalletID& receiver, Invite&& msg)
    {
        auto stored = m_keyChain->getTx(msg.m_txId);
        if (stored.is_initialized() && !stored->canResume())
        {
            return;
        }
        auto it = m_negotiators.find(msg.m_txId);
        if (it == m_negotiators.end())
        {
            LOG_VERBOSE() << "Received tx invitation " << msg.m_txId;
            bool sender = !msg.m_send;

            ByteBuffer messageBuffer;
            auto receiverAddress = m_keyChain->getAddress(receiver);
            if (receiverAddress.is_initialized())
            {
                messageBuffer.assign(receiverAddress->m_label.begin(), receiverAddress->m_label.end());
            }
            TxDescription tx{ msg.m_txId, msg.m_amount, msg.m_fee, msg.m_height, msg.m_from, receiver, move(messageBuffer), getTimestamp(), sender };
            auto r = make_shared<Negotiator>(*this, m_keyChain, tx);
            m_negotiators.emplace(tx.m_txId, r);
            m_keyChain->saveTx(tx);
            Cleaner c{ m_removedNegotiators };
            if (r->ProcessInvitation(msg))
            {
                if (m_synchronized)
                {
                    r->start();
                    r->processEvent(events::TxInvited{});
                }
                else
                {
                    m_pendingEvents.emplace_back([r]()
                    {
                        r->start();
                        r->processEvent(events::TxInvited{});
                    });
                }
            }
            else
            {
                r->processEvent(events::TxFailed{ true });
            }
        }
        else
        {
            LOG_DEBUG() << ReceiverPrefix << "Unexpected tx invitation " << msg.m_txId;
        }
    }
    
    void Wallet::handle_tx_message(const WalletID& receiver, ConfirmTransaction&& data)
    {
        LOG_DEBUG() << ReceiverPrefix << "Received sender tx confirmation " << data.m_txId;
        if (!process_event(data.m_txId, events::TxConfirmationCompleted{ data }))
        {
            LOG_DEBUG() << ReceiverPrefix << "Unexpected sender tx confirmation " << data.m_txId;
            // TODO state transition
            // m_network->close_connection(from);
        }
    }

    void Wallet::handle_tx_message(const WalletID& receiver, ConfirmInvitation&& data)
    {
        LOG_VERBOSE() << SenderPrefix << "Received tx confirmation " << data.m_txId;
        if (!process_event(data.m_txId, events::TxInvitationCompleted{ data }))
        {
            LOG_DEBUG() << SenderPrefix << "Unexpected tx confirmation " << data.m_txId;
        }
    }

    void Wallet::handle_tx_message(const WalletID& receiver, wallet::TxRegistered&& data)
    {
        process_event(data.m_txId, events::TxRegistrationCompleted{});
    }

    void Wallet::handle_tx_message(const WalletID& receiver, wallet::TxFailed&& data)
    {
        LOG_DEBUG() << "tx " << data.m_txId << " failed";
        process_event(data.m_txId, events::TxFailed(false));
    }

    bool Wallet::handle_node_message(proto::Boolean&& res)
    {
        if (m_reg_requests.empty())
        {
            LOG_DEBUG() << "Received unexpected tx registration confirmation";
            assert(m_negotiators.empty());
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
        LOG_DEBUG() << "tx " << txId << (res ? " has registered" : " has failed to register");
        if (res)
        {
            process_event(txId, events::TxRegistrationCompleted{ });
        }
        else
        {
            process_event(txId, events::TxFailed(true));
        }
    }

    void Wallet::cancel_tx(const TxID& txId)
    {
        LOG_INFO() << "Canceling tx " << txId;

        Cleaner cs{ m_removedNegotiators };
        if (auto it = m_negotiators.find(txId); it != m_negotiators.end())
        {
            it->second->processEvent(events::TxCanceled{});
        }
        else
        {
            m_keyChain->deleteTx(txId);
        }
    }

    void Wallet::delete_tx(const TxID& txId)
    {
        LOG_INFO() << "deleting tx " << txId;
        if (auto it = m_negotiators.find(txId); it == m_negotiators.end())
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
        resetSystemState();
    }

    void Wallet::resetSystemState()
    {
        ZeroObject(m_newState);
        ZeroObject(m_knownStateID);
        m_keyChain->setSystemStateID(m_knownStateID);
        m_keyChain->rollbackConfirmedUtxo(0);
    }

    void Wallet::emergencyReset()
    {
        resetSystemState();
        m_keyChain->clear();
        m_network->close_node_connection();
        m_network->connect_node();
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
            LOG_WARNING() << "Got empty proof for: " << input.m_Commitment;

            if (coin.m_status == Coin::Locked)
            {
                coin.m_status = Coin::Spent;
                m_keyChain->update(coin);
            }
            else if (coin.m_status == Coin::Unconfirmed && coin.isReward())
            {
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
                        LOG_INFO() << "Got proof for: " << input.m_Commitment;
                        coin.m_status = Coin::Unspent;
                        coin.m_maturity = proof.m_State.m_Maturity;
                        coin.m_confirmHeight = m_newState.m_Height;
                        m_newState.get_Hash(coin.m_confirmHash);
                        if (coin.isReward())
                        {
                            LOG_INFO() << "Block reward received: " << PrintableAmount(coin.m_amount);
                        }
                        if (coin.m_id == 0)
                        {
                            m_keyChain->store(coin);
                        }
                        else
                        {
                            m_keyChain->update(coin);
                        }
                    }
                    else
                    {
                        LOG_ERROR() << "Invalid proof provided: " << input.m_Commitment;
                    }
                }
            }
        }

        m_pendingUtxoProofs.pop_front();

        return exit_sync();
    }

    bool Wallet::handle_node_message(proto::NewTip&& msg)
    {
        // TODO: restore from wallet db 
        for (auto& r : m_pending_reg_requests)
        {
            register_tx(r.first, r.second);
        }

        m_pending_reg_requests.clear();

        return true;
    }

    bool Wallet::handle_node_message(proto::Hdr&& msg)
    {
        Block::SystemState::ID newID = {};
        msg.m_Description.get_ID(newID);
        
        m_newState = msg.m_Description;

        if (newID == m_knownStateID)
        {
            // here we may close connection with node
            m_keyChain->setSystemStateID(m_knownStateID);
            return close_node_connection();
        }

        if (m_knownStateID.m_Height <= Rules::HeightGenesis)
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
        for (auto& minedCoin : msg.m_Entries)
        {
            if (minedCoin.m_Active && minedCoin.m_ID.m_Height >= currentHeight) // we store coins from active branch
            {
                // coinbase 
                mined.emplace_back(Rules::get().CoinbaseEmission
                                 , Coin::Unconfirmed
                                 , minedCoin.m_ID.m_Height
                                 , MaxHeight
                                 , KeyType::Coinbase);
                if (minedCoin.m_Fees > 0)
                {
                    mined.emplace_back(minedCoin.m_Fees
                                     , Coin::Unconfirmed
                                     , minedCoin.m_ID.m_Height
                                     , MaxHeight
                                     , KeyType::Comission);
                }
            }
        }

        if (!mined.empty())
        {
            getUtxoProofs(mined);
        }
        return exit_sync();
    }

    bool Wallet::handle_node_message(proto::ProofState&& msg)
    {
        if (!IsTestMode() && !m_newState.IsValidProofState(m_knownStateID, msg.m_Proof))
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
                    m_knownStateID = {};
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
        auto n = m_pendingKernelProofs.front();
        m_pendingKernelProofs.pop_front();
        auto kernel = n->getKernel();
        assert(kernel);
        if (IsTestMode() || m_newState.IsValidProofKernel(*kernel, msg.m_Proof))
        {
            LOG_INFO() << "Got proof for tx: " << n->getTxID();
            m_pendingEvents.emplace_back([n]()
            {
                n->processEvent(events::TxOutputsConfirmed{});
            });
            get_kernel_utxo_proofs(n);
        }

        return exit_sync();
    }

    void Wallet::abort_sync()
    {
        m_syncDone = m_syncTotal = 0;
        copy(m_reg_requests.begin(), m_reg_requests.end(), back_inserter(m_pending_reg_requests));
        m_reg_requests.clear();
        m_pendingUtxoProofs.clear();

        notifySyncProgress();
    }

    void Wallet::do_fast_forward()
    {
        Block::SystemState::ID id;
        m_newState.get_ID(id);
        LOG_INFO() << "Sync up to " << id;
        // fast-forward
        enter_sync(); // Mined
        m_network->send_node_message(proto::GetMined{ m_knownStateID.m_Height });

        for (auto p : m_negotiators)
        {
            get_kernel_proof(p.second);
        }
    }

    void Wallet::get_kernel_proof(Negotiator::Ptr n)
    {
        TxKernel* kernel = n->getKernel();
        if (kernel)
        {
            proto::GetProofKernel kernelMsg = {};
            kernel->get_ID(kernelMsg.m_ID);
            m_pendingKernelProofs.push_back(n);
            kernelMsg.m_RequestHashPreimage = true;
            enter_sync();
            m_network->send_node_message(move(kernelMsg));
        }
        else // we lost kernel for some reason
        {
            get_kernel_utxo_proofs(n);
        }
    }

    void Wallet::get_kernel_utxo_proofs(Negotiator::Ptr n)
    {
        const auto& txID = n->getTxID();
        vector<Coin> unconfirmed;
        m_keyChain->visit([&](const Coin& coin)
        {
            if (coin.m_createTxId == txID && coin.m_status == Coin::Unconfirmed
                || coin.m_spentTxId == txID && coin.m_status == Coin::Locked)
            {
                unconfirmed.emplace_back(coin);
            }

            return true;
        });

        getUtxoProofs(unconfirmed);
    }

    void Wallet::getUtxoProofs(const vector<Coin>& coins)
    {
        for (auto& coin : coins)
        {
            enter_sync();
            m_pendingUtxoProofs.push_back(coin);
            Input input;
            input.m_Commitment = Commitment(m_keyChain->calcKey(coin), coin.m_amount);
            LOG_DEBUG() << "Get proof: " << input.m_Commitment;
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
                m_keyChain->setSystemStateID(m_knownStateID);
                LOG_INFO() << "Current state is " << m_knownStateID;
                m_synchronized = true;
                m_syncDone = m_syncTotal = 0;
                notifySyncProgress();
                if (!m_pendingEvents.empty())
                {
                    Cleaner c{ m_removedNegotiators };
                    for (auto& cb : m_pendingEvents)
                    {
                        cb();
                    }
                    m_pendingEvents.clear();
                }
            }
        }

        return close_node_connection();
    }

    void Wallet::notifySyncProgress()
    {
        for (auto sub : m_subscribers) sub->onSyncProgress(m_syncDone, m_syncTotal);
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
        if (m_synchronized && m_negotiators.empty())
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
        LOG_VERBOSE() << ReceiverPrefix << "sending tx for registration";
        TxBase::Context ctx;
        assert(data->IsValid(ctx));
        m_reg_requests.push_back(make_pair(txId, data));
        m_network->send_node_message(proto::NewTransaction{ data });
    }

    void Wallet::resume_negotiator(const TxDescription& tx)
    {
        Cleaner c{ m_removedNegotiators };
        auto s = make_shared<Negotiator>(*this, m_keyChain, tx);
        m_negotiators.emplace(tx.m_txId, s);

        if (m_synchronized)
        {
            s->start();
            s->processEvent(events::TxInitiated{});
        }
        else
        {
            m_pendingEvents.emplace_back([s]()
            {
                s->start();
                s->processEvent(events::TxInitiated{});
            });
        }
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
}
