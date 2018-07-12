#pragma once

#include "wallet/wallet_db.h"
#include "wallet/negotiator.h"
#include <thread>
#include <mutex>
#include <deque>
#include "core/proto.h"
#include <tuple>
namespace beam
{
    struct INetworkIO 
    {
        virtual ~INetworkIO() {}
        // wallet to wallet requests
        virtual void send_tx_message(const PeerID& to, wallet::Invite&&) = 0;
        virtual void send_tx_message(const PeerID& to, wallet::ConfirmTransaction&&) = 0;
        virtual void send_tx_message(const PeerID& to, wallet::ConfirmInvitation&&) = 0;
        virtual void send_tx_message(const PeerID& to, wallet::TxRegistered&&) = 0 ;
        virtual void send_tx_message(const PeerID& to, wallet::TxFailed&&) = 0;
        // wallet to node requests
        virtual void send_node_message(proto::NewTransaction&&) = 0;
        virtual void send_node_message(proto::GetProofUtxo&&) = 0;
		virtual void send_node_message(proto::GetHdr&&) = 0;
        virtual void send_node_message(proto::GetMined&&) = 0;
        virtual void send_node_message(proto::GetProofState&&) = 0;
        // connection control
        virtual void close_connection(const PeerID& id) = 0;
        virtual void close_node_connection() = 0;
    };

    struct IWallet
    {
        virtual ~IWallet() {}
        // wallet to wallet responses
        virtual void handle_tx_message(const PeerID&, wallet::Invite&&) = 0;
        virtual void handle_tx_message(const PeerID&, wallet::ConfirmTransaction&&) = 0;
        virtual void handle_tx_message(const PeerID&, wallet::ConfirmInvitation&&) = 0;
        virtual void handle_tx_message(const PeerID&, wallet::TxRegistered&&) = 0;
        virtual void handle_tx_message(const PeerID&, wallet::TxFailed&&) = 0;
        // node to wallet responses
        virtual bool handle_node_message(proto::Boolean&&) = 0;
        virtual bool handle_node_message(proto::ProofUtxo&&) = 0;
		virtual bool handle_node_message(proto::NewTip&&) = 0;
		virtual bool handle_node_message(proto::Hdr&&) = 0;
        virtual bool handle_node_message(proto::Mined&& msg) = 0;
        virtual bool handle_node_message(proto::Proof&& msg) = 0;

        virtual void stop_sync() = 0;
    };

    class Wallet : public IWallet
                 , public wallet::INegotiatorGateway
    {
        using Callback = std::function<void()>;
    public:
        using TxCompletedAction = std::function<void(const Uuid& tx_id)>;

        Wallet(IKeyChain::Ptr keyChain, INetworkIO& network, TxCompletedAction&& action = TxCompletedAction());
        virtual ~Wallet();

        Uuid transfer_money(const PeerID& to, Amount amount, Amount fee, bool sender, ByteBuffer&& message);
        void resume_tx(const TxDescription& tx);
        void resume_all_tx();

        void send_tx_invitation(const TxDescription& tx, wallet::Invite&&) override;
        void send_tx_confirmation(const TxDescription& tx, wallet::ConfirmTransaction&&) override;
        void on_tx_completed(const TxDescription& tx) override;
        void send_tx_failed(const TxDescription& tx) override;

        void send_tx_confirmation(const TxDescription& tx, wallet::ConfirmInvitation&&) override;
        void register_tx(const TxDescription& tx, Transaction::Ptr) override;
        void send_tx_registered(const TxDescription& tx) override;

        void handle_tx_message(const PeerID&, wallet::Invite&&) override;
        void handle_tx_message(const PeerID&, wallet::ConfirmTransaction&&) override;
        void handle_tx_message(const PeerID&, wallet::ConfirmInvitation&&) override;
        void handle_tx_message(const PeerID&, wallet::TxRegistered&&) override;
        void handle_tx_message(const PeerID&, wallet::TxFailed&&) override;

        bool handle_node_message(proto::Boolean&& res) override;
        bool handle_node_message(proto::ProofUtxo&& proof) override;
        bool handle_node_message(proto::NewTip&& msg) override;
        bool handle_node_message(proto::Hdr&& msg) override;
        bool handle_node_message(proto::Mined&& msg) override;
        bool handle_node_message(proto::Proof&& msg) override;

        void stop_sync() override;

        void handle_tx_registered(const Uuid& txId, bool res);
        void handle_tx_failed(const Uuid& txId);

    private:
        void remove_peer(const Uuid& txId);
        void getUtxoProofs(const std::vector<Coin>& coins);
        void do_fast_forward();
        bool finish_sync();
        bool close_node_connection();
        void register_tx(const Uuid& txId, Transaction::Ptr);
        void resume_negotiator(const TxDescription& tx);

        struct Cleaner
        {
            Cleaner(std::vector<wallet::Negotiator::Ptr>& t) : m_v{ t } {}
            ~Cleaner()
            {
                if (!m_v.empty())
                {
                    m_v.clear();
                }
            }
            std::vector<wallet::Negotiator::Ptr>& m_v;
        };

        template<typename Event>
        bool process_event(const Uuid& txId, Event&& event)
        {
            Cleaner cs{ m_removedNegotiators };
            if (auto it = m_negotiators.find(txId); it != m_negotiators.end())
            {
                return it->second->process_event(event);
            }
            return false;
        }

    private:

        class StateFinder;

        IKeyChain::Ptr m_keyChain;
        INetworkIO& m_network;
        std::map<PeerID, wallet::Negotiator::Ptr> m_peers;
        std::map<Uuid, wallet::Negotiator::Ptr>   m_negotiators;
        std::vector<wallet::Negotiator::Ptr>      m_removedNegotiators;
        TxCompletedAction m_tx_completed_action;
        std::deque<std::pair<Uuid, Transaction::Ptr>> m_reg_requests;
        std::vector<std::pair<Uuid, Transaction::Ptr>> m_pending_reg_requests;
        std::deque<Coin> m_pendingProofs;
        std::vector<Callback> m_pendingEvents;

        Merkle::Hash m_Definition;
        Block::SystemState::ID m_knownStateID;
        Block::SystemState::ID m_newStateID;
        std::unique_ptr<StateFinder> m_stateFinder;

        int m_syncing;
        bool m_synchronized;
    };
}
