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

        struct SendInvitationData
        {
            using Ptr = std::shared_ptr<SendInvitationData>;

            Uuid m_id;

            ECC::Amount m_amount; ///??
            ECC::Hash::Value m_message;
            ECC::Point::Native m_publicSenderBlindingExcess;
            ECC::Point::Native m_publicSenderNonce;
            std::vector<Input::Ptr> m_inputs;
            std::vector<Output::Ptr> m_outputs;
        };

        struct SendConfirmationData
        {
            using Ptr = std::shared_ptr<SendConfirmationData>;

            ECC::Scalar::Native m_senderSignature;
        };

        struct HandleInvitationData
        {
            using Ptr = std::shared_ptr<HandleInvitationData>;

            ECC::Point::Native m_publicReceiverBlindingExcess;
            ECC::Point::Native m_publicReceiverNonce;
            ECC::Scalar::Native m_receiverSignature;
        };

        struct HandleConfirmationData
        {
            using Ptr = std::shared_ptr<HandleConfirmationData>;
        };

        struct SenderState
        {
            ECC::Scalar::Native m_blindingExcess;
            ECC::Scalar::Native m_nonce;
            TxKernel m_kernel;
        };

        struct ReceiverState
        {
            Transaction m_transaction;
            TxKernel* m_kernel;
            
            ECC::Scalar::Native m_blindingExcess;
            ECC::Scalar::Native m_nonce;

            ECC::Point::Native m_publicReceiverBlindingExcess;
            
            ECC::Point::Native m_publicSenderBlindingExcess;
            ECC::Point::Native m_publicSenderNonce;

            ECC::Scalar::Native m_receiverSignature;

            ECC::Scalar::Native m_schnorrChallenge;
            ECC::Hash::Value m_message;
        };

        struct ToWallet
        {
            using Shared = std::shared_ptr<ToWallet>;

            virtual HandleInvitationData::Ptr handleInvitation(SendInvitationData& data);
            virtual HandleConfirmationData::Ptr handleConfirmation(const SendConfirmationData& data);

            ReceiverState m_state;
        };

        struct Config
        {
        };

        //Wallet();
        //Wallet(ToWallet::Shared receiver, IKeyChain::Ptr keyChain);
        Wallet(IKeyChain::Ptr keyChain, NetworkIO& network);
        virtual ~Wallet() {};

        using Result = bool;

        void sendMoney(const PeerLocator& locator, const ECC::Amount& amount);
        Result sendMoneyTo(const Config& config, uint64_t amount);

        // TODO: remove this, just for test
        void sendDummyTransaction();

        void pumpEvents(); // for test only

    private:

        Result sendInvitation(SendInvitationData& data);
        Result sendConfirmation(const SendConfirmationData& data);

        SendInvitationData::Ptr createInvitationData(const ECC::Amount& amount);

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

        ToWallet::Shared m_receiver;

        IKeyChain::Ptr m_keyChain;

        SenderState m_state;

        NetworkIO& m_network;
        std::mutex m_sendersMutex;
        std::mutex m_receiversMutex;
        std::map<Uuid, std::unique_ptr<wallet::Sender>>   m_senders;
        std::map<Uuid, std::unique_ptr<wallet::Receiver>> m_receivers;
    };
}
