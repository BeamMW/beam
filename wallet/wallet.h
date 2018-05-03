#pragma once

#include "wallet/keychain.h"
#include "wallet/sender.h"
#include "wallet/receiver.h"
//#include "utility/bridge.h"
#include <mutex>

namespace beam
{
    using Peer = uint64_t;

    struct INetworkIO {
        virtual ~INetworkIO() {}
        virtual void send_tx_invitation(Peer to, wallet::sender::InvitationData::Ptr) = 0;
        virtual void send_tx_confirmation(Peer to, wallet::sender::ConfirmationData::Ptr) = 0;
        virtual void sendChangeOutputConfirmation(Peer to) = 0;
        virtual void send_tx_confirmation(Peer to, wallet::receiver::ConfirmationData::Ptr) = 0;
        virtual void register_tx(Peer to, wallet::receiver::RegisterTxData::Ptr) = 0;
        virtual void send_tx_registered(Peer to, UuidPtr&& txId) = 0 ;
    };

    struct IWallet {
        virtual ~IWallet() {}
        virtual void handle_tx_invitation(Peer from, wallet::sender::InvitationData::Ptr) = 0;
        virtual void handle_tx_confirmation(Peer from, wallet::sender::ConfirmationData::Ptr) = 0;
        virtual void handleOutputConfirmation(Peer from) = 0;
        virtual void handle_tx_confirmation(Peer from, wallet::receiver::ConfirmationData::Ptr) = 0;
        virtual void handle_tx_registration(Peer from, UuidPtr&& txId) = 0;
        virtual void handle_tx_failed(Peer from, UuidPtr&& txId) = 0;
    };

    //struct WalletToNetworkBridge : public Bridge<INetworkIO> {
    //    BRIDGE_INIT(WalletToNetworkBridge);
    //    
    //    BRIDGE_FORWARD_IMPL(send_tx_invitation, wallet::sender::InvitationData::Ptr);
    //    BRIDGE_FORWARD_IMPL(send_tx_confirmation, wallet::sender::ConfirmationData::Ptr);
    //    //BRIDGE_FORWARD_IMPL(sendChangeOutputConfirmation(const Peer& peer);
    //    BRIDGE_FORWARD_IMPL(send_tx_confirmation, wallet::receiver::ConfirmationData::Ptr);
    //    BRIDGE_FORWARD_IMPL(register_tx, wallet::receiver::RegisterTxData::Ptr);
    //    BRIDGE_FORWARD_IMPL(send_tx_registered, UuidPtr);
    //};

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

        void sendMoney(const Peer& peer, const ECC::Amount& amount);

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
        void handle_tx_invitation(Peer from, wallet::sender::InvitationData::Ptr) override;
        void handle_tx_confirmation(Peer from, wallet::sender::ConfirmationData::Ptr) override;
        void handleOutputConfirmation(Peer from) override;
        void handle_tx_confirmation(Peer from, wallet::receiver::ConfirmationData::Ptr) override;
        void handle_tx_registration(Peer from, UuidPtr&& txId) override;
        void handle_tx_failed(Peer from, UuidPtr&& txId) override;

    private:
        ToNode::Ptr m_net;

        IKeyChain::Ptr m_keyChain;

        INetworkIO& m_network;
        
        // for now assume that all calls to wallet performs in main thread
        //std::mutex m_sendersMutex;
        //std::mutex m_receiversMutex;
        std::map<Uuid, wallet::Sender::Ptr>   m_senders;
        std::map<Uuid, wallet::Receiver::Ptr> m_receivers;
        std::vector<wallet::Sender::Ptr>      m_removedSenders;
        std::vector<wallet::Receiver::Ptr>    m_removedReceivers;
    };
}
