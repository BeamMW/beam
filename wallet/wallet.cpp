#include "wallet.h"
#include "core/serialization_adapters.h"
#include <boost/uuid/uuid.hpp>

namespace beam
{
    // temporary impl of WalletToNetwork interface
    struct WalletToNetworkDummyImpl : public Wallet::ToNode
    {
        virtual void sendTransaction(const Transaction& tx)
        {
            // serealize tx and post and to the Node TX pool
            Serializer ser;
            ser & tx;

            auto buffer = ser.buffer();

            // and send buffer to other side
        }
    };

    Wallet::Wallet(ToWallet::Shared receiver)
        : m_receiver(receiver)
    {

    }

    Wallet::Wallet()
        : m_net(std::make_unique<WalletToNetworkDummyImpl>())
    {

    }

    void Wallet::sendDummyTransaction()
    {
        // create dummy transaction here
        Transaction tx;
        m_net->sendTransaction(tx);
    }

    Wallet::Result Wallet::sendInvitation(const PartialTx& tx)
    {
        auto partialTx = m_receiver->handleInvitation(tx);

        if(partialTx.m_phase == ReceiverInitiation)
        {
            partialTx.m_phase = SenderConfirmation;

            return sendConfirmation(partialTx);
        }
        else
        {
            // error/rollback
        }

        return false;
    }

    Wallet::Result Wallet::sendConfirmation(const PartialTx& tx)
    {
        auto partialTx = m_receiver->handleConfirmation(tx);

        if(partialTx.m_phase == ReceiverConfirmation)
        {
            // all is ok, the money was sent
            // update wallet

            return true;
        }
        else
        {
            // error/rollback
        }

        return false;
    }

    Wallet::PartialTx::Ptr Wallet::createInitialPartialTx(uint64_t amount)
    {
        PartialTx::Ptr tx = std::make_unique<PartialTx>();
        tx->m_phase = SenderInitiation;
        // 1. Create transaction Uuid
        boost::uuids::uuid id;
        std::copy(id.begin(), id.end(), tx->m_id.begin());
        // 2. Set lock_height for output (current chain height)
        auto tip = m_node.getChainTip();
        uint64_t lockHeight = tip.height;
        // 3. Select inputs using desired selection strategy
        std::vector<Input> inputs;
        // 4. Create change_output

        // 5. Select blinding factor for change_output
        // 6. calculate

        return tx;
    }

    Wallet::Result Wallet::sendMoneyTo(const Config& config, uint64_t amount)
    {
        auto initialTx = createInitialPartialTx(amount);

        return sendInvitation(initialPartialTx);
    }

    Wallet::PartialTx Wallet::ToWallet::handleInvitation(const PartialTx& tx)
    {
        PartialTx res = tx;

        if(tx.m_phase == SenderInitiation)
        {
            // do all the job

            res.m_phase = ReceiverInitiation;
        }

        return res;
    }

    Wallet::PartialTx Wallet::ToWallet::handleConfirmation(const PartialTx& tx)
    {
        PartialTx res = tx;

        if(tx.m_phase == SenderConfirmation)
        {
            // do all the job

            res.m_phase = ReceiverConfirmation;
        }

        return res;
    }

}
