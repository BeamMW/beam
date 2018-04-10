#include "wallet.h"
#include "core/serialization_adapters.h"
#include <boost/uuid/uuid.hpp>
#include "core/ecc_native.h"

namespace
{
    void GenerateRandom(void* p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++)
            ((uint8_t*) p)[i] = (uint8_t) rand();
    }

    void SetRandom(ECC::uintBig& x)
    {
        GenerateRandom(x.m_pData, sizeof(x.m_pData));
    }

    void SetRandom(ECC::Scalar::Native& x)
    {
        ECC::Scalar s;
        while (true)
        {
            SetRandom(s.m_Value);
            if (!x.Import(s))
                break;
        }
    }
}

namespace ECC
{
    Context g_Ctx;
    const Context& Context::get() { return g_Ctx; }
}

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
        PartialTx::Ptr partialTx = m_receiver->handleInvitation(tx);

        if(partialTx->m_phase == ReceiverInitiation)
        {
            partialTx->m_phase = SenderConfirmation;

            return sendConfirmation(*partialTx);
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

        if(partialTx->m_phase == ReceiverConfirmation)
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
        // auto tip = m_node.getChainTip();
        // uint64_t lockHeight = tip.height;
        // 3. Select inputs using desired selection strategy
        // std::vector<Input> inputs;
        // 4. Create change_output

        // 5. Select blinding factor for change_output
        // 6. calculate

        // an attempt to implement "stingy" transaction

        ECC::Scalar::Native xS, kS;
        SetRandom(kS);
        xS = amount;
        ECC::Point::Native kSG;
        kSG = ECC::Context::get().G * kS;
        ECC::Point::Native xSG;
        xSG = ECC::Context::get().G * xS;
        // tx->m_kSG = std::make_shared(kSG);
        // tx->m_xSG = std::make_shared(xSG);

        return tx;
    }

    Wallet::Result Wallet::sendMoneyTo(const Config& config, uint64_t amount)
    {
        auto initialTx = createInitialPartialTx(amount);

        return sendInvitation(*initialTx);
    }

    Wallet::PartialTx::Ptr Wallet::ToWallet::handleInvitation(const PartialTx& tx)
    {
        PartialTx::Ptr res = std::make_unique<PartialTx>(tx);

        if (tx.m_phase == SenderInitiation)
        {
            // do all the job

            ECC::Scalar::Native xR, kR;
            SetRandom(kR);
            SetRandom(xR);
            ECC::Point::Native kRG;
            kRG = ECC::Context::get().G * kR;
            ECC::Point::Native xRG;
            xRG = ECC::Context::get().G * xR;
            // res->m_kRG = std::make_shared(kRG);
            // res->m_xRG = std::make_shared(xRG);

            ECC::Point::Native K;
            K += *(res->m_kRG);
            K += *(res->m_kSG);

            ECC::Point::Native X;
            X += *(res->m_xRG);
            X += *(res->m_xSG);

            res->m_phase = ReceiverInitiation;
        }

        return res;
    }

    Wallet::PartialTx::Ptr Wallet::ToWallet::handleConfirmation(const PartialTx& tx)
    {
        PartialTx::Ptr res = std::make_unique<PartialTx>(tx);

        if(tx.m_phase == SenderConfirmation)
        {
            // do all the job

            res->m_phase = ReceiverConfirmation;
        }

        return res;
    }

}
