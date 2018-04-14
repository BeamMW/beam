#pragma once

#include "core/common.h"
#include "core/ecc_native.h"

namespace beam
{
    struct Coin
    {
        Coin(const ECC::Scalar& key, ECC::Amount amount);
        ECC::Scalar m_key;
        ECC::Amount m_amount;
    };

    struct IKeyChain
    {
        using Ptr = std::shared_ptr<IKeyChain>;
        virtual std::vector<beam::Coin> getCoins(const ECC::Amount& amount) = 0;
        virtual ~IKeyChain(){}
    };

    struct Wallet
    {
        struct ToNode
        {
            using Ptr = std::unique_ptr<ToNode>;

            virtual void sendTransaction(const Transaction& tx) = 0;
        };

        struct SendInvitationData
        {
            using Ptr = std::shared_ptr<SendInvitationData>;

            using Uuid = std::array<uint8_t, 16>;
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
        };

        struct ReceiverState
        {
            Transaction m_transaction;

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

        Wallet();
        Wallet(ToWallet::Shared receiver, IKeyChain::Ptr keyChain);

        using Result = bool;

        Result sendMoneyTo(const Config& config, uint64_t amount);

        // TODO: remove this, just for test
        void sendDummyTransaction();

    private:

        Result sendInvitation(SendInvitationData& data);
        Result sendConfirmation(const SendConfirmationData& data);

        SendInvitationData::Ptr createInvitationData(const ECC::Amount& amount);
    private:
        ToNode::Ptr m_net;

        ToWallet::Shared m_receiver;

        IKeyChain::Ptr m_keyChain;

        SenderState m_state;
    };
}
