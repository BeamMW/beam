#pragma once

#include "wallet/keychain.h"
#include "wallet/sender.h"
#include "wallet/receiver.h"
#include <mutex>

namespace beam
{
    struct PeerLocator {};

    struct NetworkIO
    {
        virtual void sendTxInitiation(const PeerLocator& locator, wallet::sender::InvitationData::Ptr) = 0;
        virtual void sendTxConfirmation(const PeerLocator& locator, wallet::sender::ConfirmationData::Ptr) = 0;
        virtual void sendChangeOutputConfirmation(const PeerLocator& locator) = 0;
        virtual void sendTxConfirmation(const PeerLocator& locator, wallet::receiver::ConfirmationData::Ptr) = 0;
        virtual void registerTx(const PeerLocator& locator, const Transaction&) = 0;
    };

    struct IWallet
    {
        virtual void handleTxInitiation(wallet::sender::InvitationData::Ptr) = 0;
        virtual void handleTxConfirmation(wallet::sender::ConfirmationData::Ptr) = 0;
        //virtual void handleChangeOutputConfirmation(const PeerLocator& locator) = 0;
        virtual void handleTxConfirmation(wallet::receiver::ConfirmationData::Ptr) = 0;
        virtual void handleTxRegistration(const Transaction& tx) = 0;
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

        void sendMoney(const PeerLocator& locator, const ECC::Amount& amount);

        // TODO: remove this, just for test
        void sendDummyTransaction();

        void pumpEvents(); // for test only

    private:
        void sendTxInitiation(wallet::sender::InvitationData::Ptr) override;
        void sendTxConfirmation(wallet::sender::ConfirmationData::Ptr) override;
        void sendChangeOutputConfirmation() override;
        void sendTxConfirmation(wallet::receiver::ConfirmationData::Ptr) override;
        void registerTx(const Transaction&) override;
        void handleTxInitiation(wallet::sender::InvitationData::Ptr) override;
        void handleTxConfirmation(wallet::sender::ConfirmationData::Ptr) override;
        //void handleChangeOutputConfirmation(const PeerLocator& locator) override0;
        void handleTxConfirmation(wallet::receiver::ConfirmationData::Ptr) override;
        void handleTxRegistration(const Transaction& tx) override;

    private:
        ToNode::Ptr m_net;

        IKeyChain::Ptr m_keyChain;

        NetworkIO& m_network;
        std::mutex m_sendersMutex;
        std::mutex m_receiversMutex;
        std::map<Uuid, std::unique_ptr<wallet::Sender>>   m_senders;
        std::map<Uuid, std::unique_ptr<wallet::Receiver>> m_receivers;
    };
}
