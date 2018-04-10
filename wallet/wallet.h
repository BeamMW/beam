#pragma once

#include "core/common.h"

namespace beam
{
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
            Phase m_phase;

            // pub phase: PartialTxPhase,
            // pub id: Uuid,
            // pub amount: u64,
            // pub public_blind_excess: String,
            // pub public_nonce: String,
            // pub kernel_offset: String,
            // pub part_sig: String,
            // pub tx: String,
        };

        struct ToWallet
        {
            using Shared = std::shared_ptr<ToWallet>;

            virtual PartialTx handleInvitation(const PartialTx& tx);
            virtual PartialTx handleConfirmation(const PartialTx& tx);
        };

        struct Config
        {
        };

        Wallet();
        Wallet(ToWallet::Shared receiver);

        using Result = bool;

        Result sendMoneyTo(const Config& config, uint64_t amount);

        // TODO: remove this, just for test
        void sendDummyTransaction();

    private:
        Result sendInvitation(const PartialTx& tx);
        Result sendConfirmation(const PartialTx& tx);

        PartialTx createInitialPartialTx();

    private:
        ToNode::Ptr m_net;

        ToWallet::Shared m_receiver;
    };
}
