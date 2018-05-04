#pragma once

#include "wallet/keychain.h"
#include "wallet/receiver.h"
#include "wallet/sender.h"
#include <mutex>

namespace beam
{
    using PeerId = uint64_t;

    struct INetworkIO {
        virtual ~INetworkIO() {}
        virtual void send_tx_invitation(PeerId to, wallet::sender::InvitationData::Ptr) = 0;
        virtual void send_tx_confirmation(PeerId to, wallet::sender::ConfirmationData::Ptr) = 0;
        virtual void sendChangeOutputConfirmation(PeerId to) = 0;
        virtual void send_tx_confirmation(PeerId to, wallet::receiver::ConfirmationData::Ptr) = 0;
        virtual void register_tx(PeerId to, wallet::receiver::RegisterTxData::Ptr) = 0;
        virtual void send_tx_registered(PeerId to, UuidPtr&& txId) = 0 ;
    };

    struct IWallet {
        virtual ~IWallet() {}
        virtual void handle_tx_invitation(PeerId from, wallet::sender::InvitationData::Ptr) = 0;
        virtual void handle_tx_confirmation(PeerId from, wallet::sender::ConfirmationData::Ptr) = 0;
        virtual void handleOutputConfirmation(PeerId from) = 0;
        virtual void handle_tx_confirmation(PeerId from, wallet::receiver::ConfirmationData::Ptr) = 0;
        virtual void handle_tx_registration(PeerId from, UuidPtr&& txId) = 0;
        virtual void handle_tx_failed(PeerId from, UuidPtr&& txId) = 0;
    };

    struct Wallet : public IWallet
                  , public wallet::receiver::IGateway
                  , public wallet::sender::IGateway
    {
        struct ToNode
        {
            using Ptr = std::unique_ptr<ToNode>;

            virtual void sendTransaction(const Transaction& tx) = 0;
        };

        struct Config
        {
        };

        Wallet(IKeyChain::Ptr keyChain, INetworkIO& network);
        virtual ~Wallet() {};

        using Result = bool;

        void send_money(PeerId to, ECC::Amount amount);

        // TODO: remove this, just for test
        void sendDummyTransaction();

    private:
        void send_tx_invitation(wallet::sender::InvitationData::Ptr) override;
        void send_tx_confirmation(wallet::sender::ConfirmationData::Ptr) override;
        void sendChangeOutputConfirmation() override;
        void remove_sender(const Uuid& txId) override;
        void send_tx_confirmation(wallet::receiver::ConfirmationData::Ptr) override;
        void register_tx(wallet::receiver::RegisterTxData::Ptr) override;
        void send_tx_registered(UuidPtr&& txId) override;
        void remove_receiver(const Uuid& txId) override;
        void handle_tx_invitation(PeerId from, wallet::sender::InvitationData::Ptr) override;
        void handle_tx_confirmation(PeerId from, wallet::sender::ConfirmationData::Ptr) override;
        void handleOutputConfirmation(PeerId from) override;
        void handle_tx_confirmation(PeerId from, wallet::receiver::ConfirmationData::Ptr) override;
        void handle_tx_registration(PeerId from, UuidPtr&& txId) override;
        void handle_tx_failed(PeerId from, UuidPtr&& txId) override;

    private:
        ToNode::Ptr m_net;

        IKeyChain::Ptr m_keyChain;

        INetworkIO& m_network;
        
        std::map<Uuid, PeerId> m_peers;
        std::map<Uuid, wallet::Sender::Ptr>   m_senders;
        std::map<Uuid, wallet::Receiver::Ptr> m_receivers;
        std::vector<wallet::Sender::Ptr>      m_removedSenders;
        std::vector<wallet::Receiver::Ptr>    m_removedReceivers;
    };
}
