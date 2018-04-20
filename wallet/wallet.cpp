#include "wallet.h"
#include "core/serialization_adapters.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
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
    Coin::Coin(const ECC::Scalar& key, ECC::Amount amount)
        : m_amount(amount)
    {
        m_key = ECC::Scalar::Native(key);
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

    //Wallet::Wallet(ToWallet::Shared receiver, IKeyChain::Ptr keyChain)
    //    : m_receiver(receiver)
    //    , m_keyChain(keyChain)
    //{

    //}

    //Wallet::Wallet()
    //    : m_net(std::make_unique<WalletToNetworkDummyImpl>())
    //{

    //}

    Wallet::Wallet(IKeyChain::Ptr keyChain, NetworkIO& network)
        : m_network{ network }
        , m_keyChain{ keyChain }
    {
        //m_network.addListener(this);
    }

    void Wallet::sendDummyTransaction()
    {
        // create dummy transaction here
        Transaction tx;
        m_net->sendTransaction(tx);
    }

    Wallet::Result Wallet::sendInvitation(SendInvitationData& data)
    {
        auto invitationData = m_receiver->handleInvitation(data);

        // 4. Compute Sender Schnorr signature
        auto confirmationData = std::make_shared<SendConfirmationData>();
        ECC::Signature::MultiSig msig;
        msig.m_Nonce = m_state.m_nonce;
        msig.m_NoncePub = data.m_publicSenderNonce + invitationData->m_publicReceiverNonce;
        ECC::Hash::Value message;
        m_state.m_kernel.get_Hash(message);
        m_state.m_kernel.m_Signature.CoSign(confirmationData->m_senderSignature, message, m_state.m_blindingExcess, msig);
        // 1. Calculate message m
       
        // 2. Compute Schnorr challenge e
        ECC::Point::Native k;
        k = data.m_publicSenderNonce + invitationData->m_publicReceiverNonce;
        ECC::Scalar::Native e = m_state.m_kernel.m_Signature.m_e;
        //ECC::Oracle() << k << data.m_message >> e;
        //ECC::Signature::MultiSig msig;
        //msig.m_Nonce = m_state.m_nonce;
        //msig.m_NoncePub = data.m_publicSenderNonce + invitationData->m_publicReceiverNonce;
        // 3. Verify recepients Schnorr signature
        
        ECC::Point::Native s, s2;
        ECC::Scalar::Native ne;
        ne = -e;
        s = invitationData->m_publicReceiverNonce;
        s += invitationData->m_publicReceiverBlindingExcess * ne;

        s2 = ECC::Context::get().G * invitationData->m_receiverSignature;
        ECC::Point p(s), p2(s2);

        if (p.cmp(p2) != 0)
        {
            return false;
        }

        
        // ECC::Scalar::Native signature;
        // signature = m_state.m_blindingExcess;
        // signature *= e;

        
        //confirmationData->m_senderSignature = m_state.m_nonce + signature;

        return sendConfirmation(*confirmationData);
    }

    Wallet::Result Wallet::sendConfirmation(const SendConfirmationData& data)
    {
        HandleConfirmationData::Ptr res = m_receiver->handleConfirmation(data);

        return true;
    }

    Wallet::SendInvitationData::Ptr Wallet::createInvitationData(const ECC::Amount& amount)
    {
        auto coins = m_keyChain->getCoins(amount); // need to lock 
        auto res = std::make_shared<SendInvitationData>();
        res->m_amount = amount;
        m_state.m_kernel.m_Fee = 0;
        m_state.m_kernel.m_HeightMin = 0;
        m_state.m_kernel.m_HeightMax = -1;
        m_state.m_kernel.get_Hash(res->m_message);
        
        // 1. Create transaction Uuid
        boost::uuids::uuid id;
        std::copy(id.begin(), id.end(), res->m_id.begin());
        // 2. Set lock_height for output (current chain height)
        // auto tip = m_node.getChainTip();
        // uint64_t lockHeight = tip.height;
        // 3. Select inputs using desired selection strategy
        {
            m_state.m_blindingExcess = ECC::Zero;
            for (const auto& coin: coins)
            {
                Input::Ptr input = std::make_unique<Input>();
                input->m_Height = 0;
                input->m_Coinbase = false;

                ECC::Scalar::Native key(coin.m_key);
                ECC::Point::Native pt = ECC::Commitment(key, coin.m_amount);

                input->m_Commitment = pt;

                res->m_inputs.push_back(std::move(input));
                
                m_state.m_blindingExcess += key;
            }
        }
        // 4. Create change_output
        // 5. Select blinding factor for change_output
        // m_transaction.m_vOutputs.push_back(createChangeOutput(coins));
        {
            Amount change = 0;
            for (const auto &coin : coins)
            {
                change += coin.m_amount;
            }

            change -= amount;

            Output::Ptr output = std::make_unique<Output>();
            output->m_Coinbase = false;

            ECC::Scalar::Native blindingFactor;
            SetRandom(blindingFactor);
            ECC::Point::Native pt = ECC::Commitment(blindingFactor, change);
            output->m_Commitment = pt;

            output->m_pPublic.reset(new ECC::RangeProof::Public);
            output->m_pPublic->m_Value = change;
            output->m_pPublic->Create(blindingFactor);
            // TODO: need to store new key and amount in keyChain

            blindingFactor = -blindingFactor;
            m_state.m_blindingExcess += blindingFactor;

            res->m_outputs.push_back(std::move(output));
        }
        // 6. calculate tx_weight
        // 7. calculate fee
        // 8. Calculate total blinding excess for all inputs and outputs xS
        // 9. Select random nonce kS
        ECC::Signature::MultiSig msig;
        SetRandom(m_state.m_nonce);
        //msig.GenerateNonce(res->m_message, m_state.m_blindingExcess);
        //m_state.m_nonce = msig.m_Nonce;
        msig.m_Nonce = m_state.m_nonce;
        // 10. Multiply xS and kS by generator G to create public curve points xSG and kSG
        res->m_publicSenderBlindingExcess = ECC::Context::get().G * m_state.m_blindingExcess;
        res->m_publicSenderNonce = ECC::Context::get().G * m_state.m_nonce;
        // an attempt to implement "stingy" transaction

        return res;
    }

    void Wallet::sendMoney(const PeerLocator& locator, const ECC::Amount& amount)
    {
        std::lock_guard<std::mutex> lock{ m_sendersMutex };
        boost::uuids::uuid id = boost::uuids::random_generator()();
        Uuid txId;
        std::copy(id.begin(), id.end(), txId.begin());
        auto s = wallet::Sender{ *this, txId, m_keyChain, amount };
        auto p = std::make_pair(txId, std::move(s));
        auto [it, _] = m_senders.insert(std::move(p));
        it->second.start();
    }

    Wallet::Result Wallet::sendMoneyTo(const Config& config, uint64_t amount)
    {
        auto data = createInvitationData(amount);

        return sendInvitation(*data);
    }

    Wallet::HandleInvitationData::Ptr Wallet::ToWallet::handleInvitation(SendInvitationData& data)
    {
        auto res = std::make_shared<HandleInvitationData>();
        TxKernel::Ptr kernel = std::make_unique<TxKernel>();
        kernel->m_Fee = 0;
        kernel->m_HeightMin = 0;
        kernel->m_HeightMax = -1;
        m_state.m_kernel = kernel.get();
        m_state.m_transaction.m_vKernels.push_back(std::move(kernel));
        m_state.m_message = data.m_message;
        
        // res->m_publicBlindingExcess = data.m_publicBlindingExcess;
        // res->m_publicNonce = data.m_publicNonce;

        // 1. Check fee
       
        m_state.m_transaction.m_vInputs = std::move(data.m_inputs);
        m_state.m_transaction.m_vOutputs = std::move(data.m_outputs);

        // 2. Create receiver_output
        // 3. Choose random blinding factor for receiver_output
        ECC::Amount amount = data.m_amount;
        Output::Ptr output = std::make_unique<Output>();
        output->m_Coinbase = false;

        ECC::Scalar::Native blindingFactor;
        SetRandom(blindingFactor);
        ECC::Point::Native pt;
        pt = ECC::Commitment(blindingFactor, amount);
        output->m_Commitment = pt;

        output->m_pPublic.reset(new ECC::RangeProof::Public);
        output->m_pPublic->m_Value = amount;
        output->m_pPublic->Create(blindingFactor);

        blindingFactor = -blindingFactor;
        m_state.m_blindingExcess += blindingFactor;

        m_state.m_transaction.m_vOutputs.push_back(std::move(output));

        // 4. Calculate message M
        // 5. Choose random nonce
        ECC::Signature::MultiSig msig;
        SetRandom(m_state.m_nonce);
        //msig.GenerateNonce(m_state.m_message, m_state.m_blindingExcess);
        //m_state.m_nonce = msig.m_Nonce;
        msig.m_Nonce = m_state.m_nonce;
        // 6. Make public nonce and blinding factor
        m_state.m_publicReceiverBlindingExcess 
            = res->m_publicReceiverBlindingExcess 
            = ECC::Context::get().G * m_state.m_blindingExcess;

        res->m_publicReceiverNonce = ECC::Context::get().G * m_state.m_nonce;
        // 7. Compute Shnorr challenge e = H(M|K)
        //ECC::Point::Native k;
        //k = data.m_publicSenderNonce + res->m_publicReceiverNonce;

        msig.m_NoncePub = data.m_publicSenderNonce + res->m_publicReceiverNonce;

        // ECC::Scalar::Native e;

        //ECC::Oracle() << m_state.m_message << k >> m_state.m_schnorrChallenge;

        // 8. Compute recepient Shnorr signature
        //calcSignature(m_state.m_schnorrChallenge, m_state.m_nonce, m_state.m_blindingExcess, m_state.m_receiverSignature)
        m_state.m_kernel->m_Signature.CoSign(m_state.m_receiverSignature, m_state.m_message, m_state.m_blindingExcess, msig);
        
        res->m_receiverSignature = m_state.m_receiverSignature;

        m_state.m_publicSenderBlindingExcess = data.m_publicSenderBlindingExcess;
        m_state.m_publicSenderNonce = data.m_publicSenderNonce;

        return res;
    }

    Wallet::HandleConfirmationData::Ptr Wallet::ToWallet::handleConfirmation(const SendConfirmationData& data)
    {
        HandleConfirmationData::Ptr res = std::make_shared<HandleConfirmationData>();

        // 1. Verify sender's Schnor signature
        ECC::Scalar::Native ne = m_state.m_kernel->m_Signature.m_e;
        ne = -ne;
        ECC::Point::Native s, s2;

        s = m_state.m_publicSenderNonce;
        s += m_state.m_publicSenderBlindingExcess * ne;

        s2 = ECC::Context::get().G * data.m_senderSignature;
        ECC::Point p(s), p2(s2);

        if (p.cmp(p2) != 0)
        {      
            return HandleConfirmationData::Ptr();
        }

        // 2. Calculate final signature
        ECC::Scalar::Native finialSignature = data.m_senderSignature + m_state.m_receiverSignature;

        // 3. Calculate public key for excess
        ECC::Point::Native x = m_state.m_publicReceiverBlindingExcess;
        x += m_state.m_publicSenderBlindingExcess;
        // 4. Verify excess value in final transaction
        // 5. Create transaction kernel
       // TxKernel::Ptr kernel = std::make_unique<TxKernel>();
        m_state.m_kernel->m_Excess = x;
        m_state.m_kernel->m_Signature.m_k = finialSignature;      

       // m_state.m_transaction.m_vKernels.push_back(std::move(kernel));
        // 6. Create final transaction and send it to mempool
        ECC::Amount fee = 0U;
        
        // TODO: uncomment assert
     //   assert(m_state.m_transaction.IsValid(fee, 0U));
        return res;
    }

    void Wallet::sendTxInitiation(const wallet::Sender::InvitationData& data)
    {
        m_network.sendTxInitiation(PeerLocator(), data);
    }

    void Wallet::sendTxConfirmation(const wallet::Sender::ConfirmationData& data)
    {
        m_network.sendTxConfirmation(PeerLocator(), data);
    }

    void Wallet::sendChangeOutputConfirmation()
    {
        m_network.sendChangeOutputConfirmation(PeerLocator());
    }

    void Wallet::sendTxConfirmation(const wallet::Receiver::ConfirmationData& data)
    {
        m_network.sendTxConfirmation(PeerLocator(), data);
    }

    void Wallet::registerTx(const Transaction& transaction)
    {
        m_network.registerTx(PeerLocator(), transaction);
    }

    void Wallet::handleTxInitiation(const wallet::Sender::InvitationData& data)
    {
        std::lock_guard<std::mutex> lock{ m_receiversMutex };
        auto it = m_receivers.find(data.m_txId);
        if (it == m_receivers.end())
        {
            auto [it, _] = m_receivers.emplace(data.m_txId, wallet::Receiver{*this, data.m_txId});
            it->second.start();
        }
        else
        {
            // TODO: log unexpected TxInitation
        }
    }
    
    void Wallet::handleTxConfirmation(const wallet::Sender::ConfirmationData& data)
    {
        std::lock_guard<std::mutex> lock{ m_receiversMutex };
        auto it = m_receivers.find(data.m_txId);
        if (it != m_receivers.end())
        {
            it->second.enqueueEvent(wallet::Receiver::TxConfirmationCompleted());
        }
        else
        {
            // TODO: log unexpected TxConfirmation
        }
    }

    //void Wallet::handleChangeOutputConfirmation(const PeerLocator& locator)
    //{

    //}
    
    void Wallet::handleTxConfirmation(const wallet::Receiver::ConfirmationData& data)
    {
        std::lock_guard<std::mutex> lock{ m_sendersMutex };
        auto it = m_senders.find(data.m_txId);
        if (it != m_senders.end())
        {
            it->second.enqueueEvent(wallet::Sender::TxInitCompleted());
        }
        else
        {
            // TODO: log unexpected TxConfirmation
        }
    }

    void Wallet::handleTxRegistration(const Transaction& tx)
    {
        std::lock_guard<std::mutex> lock{ m_receiversMutex };
        if (!m_receivers.empty())
        {
            m_receivers.begin()->second.enqueueEvent(wallet::Receiver::TxRegistrationCompleted());
        }
        //auto it = m_receivers.find(data.m_txId);
        //if (it != m_receivers.end())
        //{
        //    it->second.enqueueEvent(wallet::Receiver::TxConfirmationCompleted());
        //}
        //else
        //{
        //    // TODO: log unexpected TxConfirmation
        //}
    }

    void Wallet::pumpEvents()
    {
        {
            std::lock_guard<std::mutex> lock{ m_sendersMutex };
            for (auto& s : m_senders)
            {
                s.second.executeQueuedEvents();
            }
        }
        {
            std::lock_guard<std::mutex> lock{ m_receiversMutex };
            for (auto& r : m_receivers)
            {
                r.second.executeQueuedEvents();
            }
        }
    }
}
