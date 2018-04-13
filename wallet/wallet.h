#pragma once

#include "core/common.h"
#include "core/ecc_native.h"

namespace beam
{
    struct Coin
    {
        Coin(uint64_t key, ECC::Amount amount);
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

        enum Phase
        {
            SenderInitiation,
            ReceiverInitiation,
            SenderConfirmation,
            ReceiverConfirmation,
        };

        struct PartialTx
        {
            // TODO: impl constructors
            PartialTx() {}
            PartialTx(ECC::Amount amount, const std::vector<Coin>& coins);
            PartialTx(const PartialTx& from) {}

            using Ptr = std::unique_ptr<PartialTx>;
            using Uuid = std::array<uint8_t, 16>;
            Phase m_phase;
            Uuid  m_id;
            
            ECC::Amount m_amount;
            
            beam::Transaction m_transaction;

            struct TxData
            {
                ECC::Scalar::Native m_blindingExcess;
                ECC::Scalar::Native m_nonce;
                ECC::Point::Native  m_publicBlindingExcess;
                ECC::Point::Native  m_publicNonce;
                ECC::Scalar::Native m_signature;
            };
            TxData m_senderData;
            TxData m_receiverData;
            

            // pub phase: PartialTxPhase,
            // pub id: Uuid,
            // pub amount: u64,
            // pub public_blind_excess: String,
            // pub public_nonce: String,
            // pub kernel_offset: String,
            // pub part_sig: String,
            // pub tx: String,
        private:
            // std::vector<Input::Ptr> createInputs(const std::vector<Coin>& coins);
            // Output::Ptr createChangeOutput(const std::vector<Coin>& coins);
        };

        struct SendInvitationData
        {
            using Ptr = std::shared_ptr<SendInvitationData>;

            SendInvitationData(ECC::Amount amount, const std::vector<Coin>& coins);

            using Uuid = std::array<uint8_t, 16>;
            Uuid  m_id;

            ECC::Amount m_amount;
            beam::Transaction m_transaction;

            /////////////////////////////////////////////////

            ECC::Point::Native m_publicBlindingExcess;
            ECC::Point::Native m_publicNonce;
            ECC::Scalar::Native m_signature;
        };

        struct SendConfirmationData
        {
            using Ptr = std::shared_ptr<SendConfirmationData>;

            ECC::Scalar::Native m_signature;
        };

        struct HandleInvitationData
        {
            using Ptr = std::shared_ptr<HandleInvitationData>;

            ECC::Point::Native m_publicBlindingExcess;
            ECC::Point::Native m_publicNonce;
            ECC::Scalar::Native m_signature;
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
            ECC::Scalar::Native m_blindingExcess;
            ECC::Scalar::Native m_nonce;
        };

        struct ToWallet
        {
            using Shared = std::shared_ptr<ToWallet>;

            virtual HandleInvitationData::Ptr handleInvitation(const SendInvitationData& data);
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

        Result sendInvitation(const SendInvitationData& data);
        Result sendConfirmation(const SendConfirmationData& data);

        SendInvitationData::Ptr createInvitationData(const ECC::Amount& amount);
    private:
        ToNode::Ptr m_net;

        ToWallet::Shared m_receiver;

        IKeyChain::Ptr m_keyChain;

        SenderState m_state;
    };
}
