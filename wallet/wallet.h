#pragma once

#include "wallet/keychain.h"
#include "wallet/receiver.h"
#include "wallet/sender.h"
#include <thread>
#include <mutex>
#include <queue>
#include "core/proto.h"
#include <tuple>
namespace beam
{
    using PeerId = uint64_t;

    struct INetworkIO 
    {
        virtual ~INetworkIO() {}
        // wallet to wallet requests
        virtual void send_tx_message(PeerId to, wallet::sender::InvitationData::Ptr&&) = 0;
        virtual void send_tx_message(PeerId to, wallet::sender::ConfirmationData::Ptr&&) = 0;
        virtual void send_tx_message(PeerId to, wallet::receiver::ConfirmationData::Ptr&&) = 0;
        virtual void send_tx_message(PeerId to, wallet::TxRegisteredData&&) = 0 ;
        // wallet to node requests
        virtual void send_node_message(proto::NewTransaction&&) = 0;
        virtual void send_node_message(proto::GetProofUtxo&&) = 0;
		virtual void send_node_message(proto::GetHdr&&) = 0;
        virtual void send_node_message(proto::GetMined&&) = 0;
        // connection control
        virtual void close_connection(PeerId id) = 0;
    };

    struct IWallet
    {
        virtual ~IWallet() {}
        // wallet to wallet responses
        virtual void handle_tx_message(PeerId, wallet::sender::InvitationData::Ptr&&) = 0;
        virtual void handle_tx_message(PeerId, wallet::sender::ConfirmationData::Ptr&&) = 0;
        virtual void handle_tx_message(PeerId, wallet::receiver::ConfirmationData::Ptr&&) = 0;
        virtual void handle_tx_message(PeerId, wallet::TxRegisteredData&&) = 0;
        // node to wallet responses
        virtual void handle_node_message(proto::Boolean&&) = 0;
        virtual void handle_node_message(proto::ProofUtxo&&) = 0;
		virtual void handle_node_message(proto::NewTip&&) = 0;
		virtual void handle_node_message(proto::Hdr&&) = 0;
        virtual void handle_node_message(proto::Mined&& msg) = 0;
        // connection control
        virtual void handle_connection_error(PeerId) = 0;
    };

    class Wallet : public IWallet
                 , public wallet::receiver::IGateway
                 , public wallet::sender::IGateway
    {
    public:
        using TxCompletedAction = std::function<void(const Uuid& tx_id)>;

        Wallet(IKeyChain::Ptr keyChain, INetworkIO& network, TxCompletedAction&& action = TxCompletedAction());
        virtual ~Wallet();

        void transfer_money(PeerId to, ECC::Amount&& amount);

        void send_tx_invitation(wallet::sender::InvitationData::Ptr) override;
        void send_tx_confirmation(wallet::sender::ConfirmationData::Ptr) override;
        void on_tx_completed(const Uuid& txId) override;
        void send_tx_failed(const Uuid& txId) override;

        void remove_sender(const Uuid& txId);
        void remove_receiver(const Uuid& txId);

        void send_tx_confirmation(wallet::receiver::ConfirmationData::Ptr) override;
        void register_tx(const Uuid&, Transaction::Ptr) override;
        void send_tx_registered(UuidPtr&& txId) override;

        void handle_tx_message(PeerId from, wallet::sender::InvitationData::Ptr&&) override;
        void handle_tx_message(PeerId from, wallet::sender::ConfirmationData::Ptr&&) override;
        void handle_tx_message(PeerId from, wallet::receiver::ConfirmationData::Ptr&&) override;
        void handle_tx_message(PeerId from, wallet::TxRegisteredData&&) override;
        void handle_node_message(proto::Boolean&& res) override;
        void handle_node_message(proto::ProofUtxo&& proof) override;
		void handle_node_message(proto::NewTip&& msg) override;
		void handle_node_message(proto::Hdr&& msg) override;
        void handle_node_message(proto::Mined&& msg) override;
        void handle_connection_error(PeerId from) override;

        void handle_tx_registered(const Uuid& txId, bool res);

        template<typename Func>
        void send_tx_message(const Uuid& txId, Func f)
        {
            if (auto it = m_peers.find(txId); it != m_peers.end())
            {
                f(it->second);
            }
            else
            {
                assert(false && "no peers");
                LOG_ERROR() << "Attempt to send message for unknown tx";
            }
        }

        void remove_peer(const Uuid& txId);
    private:
        IKeyChain::Ptr m_keyChain;
        INetworkIO& m_network;
        std::map<Uuid, PeerId> m_peers;
        std::map<Uuid, wallet::Sender::Ptr>   m_senders;
        std::map<Uuid, wallet::Receiver::Ptr> m_receivers;
        std::vector<wallet::Sender::Ptr>      m_removed_senders;
        std::vector<wallet::Receiver::Ptr>    m_removed_receivers;
        TxCompletedAction m_tx_completed_action;
        std::queue<Uuid> m_node_requests_queue;
    };
}
