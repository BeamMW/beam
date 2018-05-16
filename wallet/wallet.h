#pragma once

#include "wallet/keychain.h"
#include "wallet/receiver.h"
#include "wallet/sender.h"
#include <thread>
#include <mutex>
#include <queue>

namespace beam
{
    using PeerId = uint64_t;
    using None = int32_t; // TODO: change for real data

    struct INetworkIO {
        virtual ~INetworkIO() {}
        virtual void send_tx_invitation(PeerId to, wallet::sender::InvitationData::Ptr&&) = 0;
        virtual void send_tx_confirmation(PeerId to, wallet::sender::ConfirmationData::Ptr&&) = 0;
        virtual void send_output_confirmation(PeerId to, None&&) = 0;
        virtual void send_tx_confirmation(PeerId to, wallet::receiver::ConfirmationData::Ptr&&) = 0;
        virtual void register_tx(Transaction::Ptr&&) = 0;
        virtual void send_tx_registered(PeerId to, UuidPtr&& txId) = 0 ;
    };

    struct IWallet {
        virtual ~IWallet() {}
        virtual void handle_tx_invitation(PeerId from, wallet::sender::InvitationData::Ptr&&) = 0;
        virtual void handle_tx_confirmation(PeerId from, wallet::sender::ConfirmationData::Ptr&&) = 0;
        virtual void handle_output_confirmation(PeerId from, None&&) = 0;
        virtual void handle_tx_confirmation(PeerId from, wallet::receiver::ConfirmationData::Ptr&&) = 0;
        virtual void handle_tx_registration(bool&& res) = 0;
        virtual void handle_tx_failed(PeerId from, UuidPtr&& txId) = 0;
    };

    class Wallet : public IWallet
                 , public wallet::receiver::IGateway
                 , public wallet::sender::IGateway
    {
    public:
        using TxCompletedAction = std::function<void(const Uuid& tx_id)>;

        Wallet(IKeyChain::Ptr keyChain, INetworkIO& network, TxCompletedAction&& action = TxCompletedAction());
        virtual ~Wallet() {};

        void send_money(PeerId to, ECC::Amount&& amount);

        void send_tx_invitation(wallet::sender::InvitationData::Ptr) override;
        void send_tx_confirmation(wallet::sender::ConfirmationData::Ptr) override;
        void on_tx_completed(const Uuid& txId) override;
        void send_output_confirmation() override;
        void remove_sender(const Uuid& txId);
        void remove_receiver(const Uuid& txId);
        void send_tx_confirmation(wallet::receiver::ConfirmationData::Ptr) override;
        void register_tx(const Uuid&, Transaction::Ptr) override;
        void send_tx_registered(UuidPtr&& txId) override;
        void handle_tx_invitation(PeerId from, wallet::sender::InvitationData::Ptr&&) override;
        void handle_tx_confirmation(PeerId from, wallet::sender::ConfirmationData::Ptr&&) override;
        void handle_output_confirmation(PeerId from, None&&) override;
        void handle_tx_confirmation(PeerId from, wallet::receiver::ConfirmationData::Ptr&&) override;
        void handle_tx_registration(bool&& res) override;
        void handle_tx_failed(PeerId from, UuidPtr&& txId) override;

    private:
        IKeyChain::Ptr m_keyChain;
        INetworkIO& m_network;
        std::map<Uuid, PeerId> m_peers;
        std::map<Uuid, wallet::Sender::Ptr>   m_senders;
        std::map<Uuid, wallet::Receiver::Ptr> m_receivers;
        std::vector<wallet::Sender::Ptr>      m_removedSenders;
        std::vector<wallet::Receiver::Ptr>    m_removedReceivers;
        uint64_t m_node_id;
        TxCompletedAction m_tx_completed_action;
        std::queue<Uuid> m_registration_queue;
#ifndef NDEBUG
        std::thread::id m_tid;
        void check_thread() { assert(m_tid == std::this_thread::get_id()); }
#endif
    };
}
