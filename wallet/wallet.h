#pragma once

#include "wallet/keychain.h"
#include "wallet/sender.h"
#include "wallet/receiver.h"
#include <mutex>

namespace beam
{
    struct Peer {};

    struct NetworkIO
    {
        virtual void sendTxInitiation(const Peer& peer, wallet::sender::InvitationData::Ptr) = 0;
        virtual void sendTxConfirmation(const Peer& peer, wallet::sender::ConfirmationData::Ptr) = 0;
        virtual void sendChangeOutputConfirmation(const Peer& peer) = 0;
        virtual void sendTxConfirmation(const Peer& peer, wallet::receiver::ConfirmationData::Ptr) = 0;
        virtual void registerTx(const Peer& peer, const Uuid& txId, TransactionPtr) = 0;
        virtual void sendTxRegistered(const Peer& peer, const Uuid& txId) = 0 ;
    };

    struct IWallet
    {
        virtual void handleTxInitiation(wallet::sender::InvitationData::Ptr) = 0;
        virtual void handleTxConfirmation(wallet::sender::ConfirmationData::Ptr) = 0;
        virtual void handleOutputConfirmation(const Peer& peer) = 0;
        virtual void handleTxConfirmation(wallet::receiver::ConfirmationData::Ptr) = 0;
        virtual void handleTxRegistration(const Uuid& txId) = 0;
        virtual void handleTxFailed(const Uuid& txId) = 0;
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

        Wallet(IKeyChain::Ptr keyChain, NetworkIO& network);
        virtual ~Wallet() {};

        using Result = bool;

        void sendMoney(const Peer& peer, const ECC::Amount& amount);

        // TODO: remove this, just for test
        void sendDummyTransaction();

    private:
        void sendTxInitiation(wallet::sender::InvitationData::Ptr) override;
        void sendTxConfirmation(wallet::sender::ConfirmationData::Ptr) override;
        void sendChangeOutputConfirmation() override;
        void removeSender(const Uuid& txId) override;
        void sendTxConfirmation(wallet::receiver::ConfirmationData::Ptr) override;
        void registerTx(const Uuid& txId, TransactionPtr) override;
        void sendTxRegistered(const Uuid& txId) override;
        void removeReceiver(const Uuid& txId) override;
        void handleTxInitiation(wallet::sender::InvitationData::Ptr) override;
        void handleTxConfirmation(wallet::sender::ConfirmationData::Ptr) override;
        void handleOutputConfirmation(const Peer&) override;
        void handleTxConfirmation(wallet::receiver::ConfirmationData::Ptr) override;
        void handleTxRegistration(const Uuid& txId) override;
        void handleTxFailed(const Uuid& txId) override;

    private:
        ToNode::Ptr m_net;

        IKeyChain::Ptr m_keyChain;

        NetworkIO& m_network;
        
        // for now assume that all calls to wallet performs in main thread
        //std::mutex m_sendersMutex;
        //std::mutex m_receiversMutex;
        std::map<Uuid, wallet::Sender::Ptr>   m_senders;
        std::map<Uuid, wallet::Receiver::Ptr> m_receivers;
        std::vector<wallet::Sender::Ptr>      m_removedSenders;
        std::vector<wallet::Receiver::Ptr>    m_removedReceivers;
    };
}
