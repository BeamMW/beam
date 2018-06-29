#pragma once

#include "wallet/wallet_db.h"
#include "wallet/sender.h"
#include <thread>
#include <mutex>
#include <deque>
#include "core/proto.h"
#include <tuple>
namespace beam
{
    using PeerId = uint64_t;

    struct INetworkIO 
    {
        virtual ~INetworkIO() {}
        // wallet to wallet requests
        virtual void send_tx_message(PeerId to, wallet::InviteReceiver&&) = 0;
        virtual void send_tx_message(PeerId to, wallet::ConfirmTransaction&&) = 0;
        virtual void send_tx_message(PeerId to, wallet::ConfirmInvitation&&) = 0;
        virtual void send_tx_message(PeerId to, wallet::TxRegistered&&) = 0 ;
        virtual void send_tx_message(PeerId to, wallet::TxFailed&&) = 0;
        // wallet to node requests
        virtual void send_node_message(proto::NewTransaction&&) = 0;
        virtual void send_node_message(proto::GetProofUtxo&&) = 0;
		virtual void send_node_message(proto::GetHdr&&) = 0;
        virtual void send_node_message(proto::GetMined&&) = 0;
        // connection control
        virtual void close_connection(PeerId id) = 0;
        virtual void close_node_connection() = 0;
    };

    struct IWallet
    {
        virtual ~IWallet() {}
        // wallet to wallet responses
        virtual void handle_tx_message(PeerId, wallet::InviteReceiver&&) = 0;
        virtual void handle_tx_message(PeerId, wallet::ConfirmTransaction&&) = 0;
        virtual void handle_tx_message(PeerId, wallet::ConfirmInvitation&&) = 0;
        virtual void handle_tx_message(PeerId, wallet::TxRegistered&&) = 0;
        virtual void handle_tx_message(PeerId, wallet::TxFailed&&) = 0;
        // node to wallet responses
        virtual bool handle_node_message(proto::Boolean&&) = 0;
        virtual bool handle_node_message(proto::ProofUtxo&&) = 0;
		virtual bool handle_node_message(proto::NewTip&&) = 0;
		virtual bool handle_node_message(proto::Hdr&&) = 0;
        virtual bool handle_node_message(proto::Mined&& msg) = 0;
        // connection control
        virtual void handle_connection_error(PeerId) = 0;
        virtual void stop_sync() = 0;
    };

    class Wallet : public IWallet
                 , public wallet::receiver::IGateway
                 , public wallet::sender::IGateway
    {
        using Callback = std::function<void()>;
    public:
        using TxCompletedAction = std::function<void(const Uuid& tx_id)>;

        Wallet(IKeyChain::Ptr keyChain, INetworkIO& network, TxCompletedAction&& action = TxCompletedAction());
        virtual ~Wallet();

        Uuid transfer_money(PeerId to, Amount amount, ByteBuffer&& message);
        void resume_tx(const TxDescription& tx);
        void resume_all_tx();

        void send_tx_invitation(const TxDescription& tx, wallet::InviteReceiver&&) override;
        void send_tx_confirmation(const TxDescription& tx, wallet::ConfirmTransaction&&) override;
        void on_tx_completed(const TxDescription& tx) override;
        void send_tx_failed(const TxDescription& tx) override;

        void send_tx_confirmation(const TxDescription& tx, wallet::ConfirmInvitation&&) override;
        void register_tx(const TxDescription& tx, Transaction::Ptr) override;
        void send_tx_registered(const TxDescription& tx) override;

        void handle_tx_message(PeerId, wallet::InviteReceiver&&) override;
        void handle_tx_message(PeerId, wallet::ConfirmTransaction&&) override;
        void handle_tx_message(PeerId, wallet::ConfirmInvitation&&) override;
        void handle_tx_message(PeerId, wallet::TxRegistered&&) override;
        void handle_tx_message(PeerId, wallet::TxFailed&&) override;

        bool handle_node_message(proto::Boolean&& res) override;
        bool handle_node_message(proto::ProofUtxo&& proof) override;
        bool handle_node_message(proto::NewTip&& msg) override;
        bool handle_node_message(proto::Hdr&& msg) override;
        bool handle_node_message(proto::Mined&& msg) override;
        void handle_connection_error(PeerId from) override;
        void stop_sync() override;

        void handle_tx_registered(const Uuid& txId, bool res);
        void handle_tx_failed(const Uuid& txId);

    private:
        void remove_peer(const Uuid& txId);
        void getUtxoProofs(const std::vector<Coin>& coins);
        bool finish_sync();
        bool close_node_connection();
        void register_tx(const Uuid& txId, Transaction::Ptr);
        void resume_sender(const TxDescription& tx);

        template<typename Event>
        bool process_event(const Uuid& txId, Event&& event)
        {
            Cleaner<std::vector<wallet::Sender::Ptr> > cs{ m_removed_senders };
            if (auto it = m_senders.find(txId); it != m_senders.end())
            {
                return it->second->process_event(event);
            }
            return false;
        }

    private:
        IKeyChain::Ptr m_keyChain;
        INetworkIO& m_network;
        std::map<PeerId, wallet::Sender::Ptr> m_peers;
        std::map<Uuid, wallet::Sender::Ptr>   m_senders;
        std::vector<wallet::Sender::Ptr>      m_removed_senders;
        TxCompletedAction m_tx_completed_action;
        std::deque<std::pair<Uuid, TransactionPtr>> m_reg_requests;
        std::vector<std::pair<Uuid, TransactionPtr>> m_pending_reg_requests;
        std::deque<Coin> m_pendingProofs;
        std::vector<Callback> m_pendingEvents;

        Merkle::Hash m_Definition;
        Block::SystemState::ID m_knownStateID;
        Block::SystemState::ID m_newStateID;
        int m_syncing;
        bool m_synchronized;
    };
}
