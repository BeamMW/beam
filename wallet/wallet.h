#pragma once

#include "core/common.h"

namespace ECC{
    class Point::Native;
}

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
            // TODO: impl constructors
            PartialTx() {}
            PartialTx(const PartialTx& from) {}

            using Ptr = std::unique_ptr<PartialTx>;
            using Uuid = std::array<uint8_t, 16>;
            Phase m_phase;
            Uuid  m_id;
            
            // TODO: replace with unique_ptr?
            std::shared_ptr<ECC::Point::Native> m_kSG, m_xSG;
            std::shared_ptr<ECC::Point::Native> m_kRG, m_xRG;

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

            virtual PartialTx::Ptr handleInvitation(const PartialTx& tx);
            virtual PartialTx::Ptr handleConfirmation(const PartialTx& tx);
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

        PartialTx::Ptr createInitialPartialTx(uint64_t amount);

    private:
        ToNode::Ptr m_net;

        ToWallet::Shared m_receiver;
    };
}
