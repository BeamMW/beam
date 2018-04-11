#include "wallet.h"
#include "core/serialization_adapters.h"
#include <boost/uuid/uuid.hpp>
#include "core/ecc_native.h"
#include <algorithm>

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

    Coin::Coin(uint64_t key, ECC::Amount amount)
        : m_amount(amount)
    {
        ECC::Scalar::Native s;
        s = key;
        s.Export(m_key);
    }

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

    Wallet::PartialTx::PartialTx(ECC::Amount amount, const std::vector<Coin>& coins)
        : m_phase(SenderInitiation)
        , m_amount(amount)
    {
        // 1. Create transaction Uuid
        boost::uuids::uuid id;
        std::copy(id.begin(), id.end(), m_id.begin());
        // 2. Set lock_height for output (current chain height)
        // auto tip = m_node.getChainTip();
        // uint64_t lockHeight = tip.height;
        // 3. Select inputs using desired selection strategy
        m_transaction.m_vInputs = createInputs(coins);
        // 4. Create change_output
        // 5. Select blinding factor for change_output
        m_transaction.m_vOutputs.push_back(createChangeOutput(coins));
        // 6. calculate tx_weight
        // 7. calculate fee
        // 8. Calculate total blinding excess for all inputs and outputs xS

        // 9. Select random nonce kS
        ECC::Scalar::Native nonce;
        SetRandom(nonce);
        nonce.Export(m_nonce);
        // 10. Multiply xS and kS by generator G to create public curve points xSG and kSG

        // an attempt to implement "stingy" transaction
    }


    std::vector<Input::Ptr> Wallet::PartialTx::createInputs(const std::vector<Coin>& coins)
    {
        std::vector<Input::Ptr> inputs{coins.size()};
        ECC::Scalar::Native totalBlindingExcess;
        totalBlindingExcess = ECC::Zero;
        for (const auto& coin: coins)
        {
            beam::Input::Ptr input{new beam::Input};
            input->m_Height = 0;
            input->m_Coinbase = false;

            ECC::Scalar::Native key;
            key.Import(coin.m_key);
            ECC::Point::Native pt;
			pt = ECC::Commitment(key, coin.m_amount);
			pt.Export(input->m_Commitment);

            inputs.push_back(std::move(input));
            
            totalBlindingExcess += key;
        }
        totalBlindingExcess.Export(m_totalBlindingExcess);
        return inputs;        
    }

    Output::Ptr Wallet::PartialTx::createChangeOutput(const std::vector<Coin>& coins)
    {
        Amount change = 0;
        for(const auto& coin : coins)
        {
            change += coin.m_amount;
        }
        change -= m_amount;

        Output::Ptr output = std::make_unique<Output>();
        output->m_Coinbase = false;

        ECC::Scalar::Native blindingFactor;
        SetRandom(blindingFactor);
        ECC::Point::Native pt;
        pt = ECC::Commitment(blindingFactor, change);
        pt.Export(output->m_Commitment); 

		output->m_pPublic.reset(new ECC::RangeProof::Public);
		output->m_pPublic->m_Value = change;
		output->m_pPublic->Create(blindingFactor);
        // TODO: need to store new key and amount in keyChain
        ECC::Scalar::Native totalBlindingExcess;
        totalBlindingExcess.Import(m_totalBlindingExcess);
        blindingFactor = -blindingFactor;
        totalBlindingExcess += blindingFactor;
        totalBlindingExcess.Export(m_totalBlindingExcess);

        return output;
    }

    Wallet::Wallet(ToWallet::Shared receiver, IKeyChain::Ptr keyChain)
        : m_receiver(receiver)
        , m_keyChain(keyChain)
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

    Wallet::PartialTx::Ptr Wallet::createInitialPartialTx(const ECC::Amount& amount)
    {
        auto coins = m_keyChain->getCoins(amount); // need to lock 
        return std::make_unique<PartialTx>(amount, coins);
    }

    Wallet::Result Wallet::sendMoneyTo(const Config& config, uint64_t amount)
    {
        auto initialTx = createInitialPartialTx(amount);

        return sendInvitation(*initialTx);
    }

    Wallet::PartialTx::Ptr Wallet::ToWallet::handleInvitation(const PartialTx& tx)
    {
        PartialTx::Ptr res = std::make_unique<PartialTx>();

        if (tx.m_phase == SenderInitiation)
        {
            // do all the job


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
