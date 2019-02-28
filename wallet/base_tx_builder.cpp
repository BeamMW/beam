// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "base_tx_builder.h"
#include "core/block_crypt.h"
#include "wallet_transaction.h"

// TODO: getrandom not available until API 28 in the Android NDK 17b
// https://github.com/boostorg/uuid/issues/76
#if defined(__ANDROID__)
#define BOOST_UUID_RANDOM_PROVIDER_DISABLE_GETRANDOM 1
#endif

#include <boost/uuid/uuid_generators.hpp>
#include <numeric>
#include "utility/logger.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    BaseTxBuilder::BaseTxBuilder(BaseTransaction& tx, SubTxID subTxID, const AmountList& amountList, Amount fee)
        : m_Tx{ tx }
        , m_SubTxID(subTxID)
        , m_AmountList{ amountList }
        , m_Fee{ fee }
        , m_Change{0}
        , m_MinHeight{0}
        , m_MaxHeight{MaxHeight}
    {
    }

    void BaseTxBuilder::SelectInputs()
    {
        CoinIDList preselectedCoinIDs;
        vector<Coin> coins;
        Amount preselectedAmount = 0;
        if (m_Tx.GetParameter(TxParameterID::PreselectedCoins, preselectedCoinIDs, m_SubTxID) && !preselectedCoinIDs.empty())
        {
            coins = m_Tx.GetWalletDB()->getCoinsByID(preselectedCoinIDs);
            for (auto& coin : coins)
            {
                preselectedAmount += coin.getAmount();
                coin.m_spentTxId = m_Tx.GetTxID();
            }
            m_Tx.GetWalletDB()->save(coins);
        }
        Amount amountWithFee = GetAmount() + m_Fee;
        if (preselectedAmount < amountWithFee)
        {
            auto selectedCoins = m_Tx.GetWalletDB()->selectCoins(amountWithFee - preselectedAmount);
            copy(selectedCoins.begin(), selectedCoins.end(), back_inserter(coins));
        }

        if (coins.empty())
        {
            Totals totals(*m_Tx.GetWalletDB());

            LOG_ERROR() << m_Tx.GetTxID() << " You only have " << PrintableAmount(totals.Avail);
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoInputs);
        }

        m_Inputs.reserve(m_Inputs.size() + coins.size());
        Amount total = 0;
        for (auto& coin : coins)
        {
            coin.m_spentTxId = m_Tx.GetTxID();

            auto& input = m_Inputs.emplace_back(make_unique<Input>());

            Scalar::Native blindingFactor;
            m_Tx.GetWalletDB()->calcCommitment(blindingFactor, input->m_Commitment, coin.m_ID);

            m_Offset += blindingFactor;
            total += coin.m_ID.m_Value;
        }

        m_Change += total - amountWithFee;

        m_Tx.SetParameter(TxParameterID::Change, m_Change, false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);

        m_Tx.GetWalletDB()->save(coins);
    }

    void BaseTxBuilder::AddChangeOutput()
    {
        if (m_Change == 0)
        {
            return;
        }

        AddOutput(m_Change, true);
    }

    void BaseTxBuilder::AddOutput(Amount amount, bool bChange)
    {
        m_Outputs.push_back(CreateOutput(amount, bChange));
    }

    bool BaseTxBuilder::FinalizeOutputs()
    {
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);
        
        // TODO: check transaction size here

        return true;
    }

    Output::Ptr BaseTxBuilder::CreateOutput(Amount amount, bool bChange)
    {
        Coin newUtxo(amount);
        newUtxo.m_createTxId = m_Tx.GetTxID();
        if (bChange)
        {
            newUtxo.m_ID.m_Type = Key::Type::Change;
        }
        m_Tx.GetWalletDB()->store(newUtxo);

        Scalar::Native blindingFactor;
        Output::Ptr output = make_unique<Output>();
        output->Create(blindingFactor, *m_Tx.GetWalletDB()->get_ChildKdf(newUtxo.m_ID.m_SubIdx), newUtxo.m_ID, *m_Tx.GetWalletDB()->get_MasterKdf());

        blindingFactor = -blindingFactor;
        m_Offset += blindingFactor;

        return output;
    }

    void BaseTxBuilder::CreateKernel()
    {
        // create kernel
        assert(!m_Kernel);
        m_Kernel = make_unique<TxKernel>();
        m_Kernel->m_Fee = m_Fee;
        m_Kernel->m_Height.m_Min = m_MinHeight;
        m_Kernel->m_Height.m_Max = m_MaxHeight;
        m_Kernel->m_Commitment = Zero;

        Hash::Value peerLockImage;
        if (m_Tx.GetParameter(TxParameterID::PeerLockImage, peerLockImage, m_SubTxID))
        {
            m_PeerLockImage = make_unique<Hash::Value>(move(peerLockImage));
        }

        uintBig preImage;
        if (m_Tx.GetParameter(TxParameterID::PreImage, preImage, m_SubTxID))
        {
            m_Kernel->m_pHashLock = make_unique<TxKernel::HashLock>();
            m_Kernel->m_pHashLock->m_Preimage = move(preImage);
        }

        if (!m_Tx.GetParameter(TxParameterID::BlindingExcess, m_BlindingExcess, m_SubTxID))
        {
            Key::ID kid;
            kid.m_Idx = m_Tx.GetWalletDB()->AllocateKidRange(1);
            kid.m_Type = FOURCC_FROM(KerW);
            kid.m_SubIdx = 0;

            m_Tx.GetWalletDB()->get_MasterKdf()->DeriveKey(m_BlindingExcess, kid);

            m_Tx.SetParameter(TxParameterID::BlindingExcess, m_BlindingExcess, false, m_SubTxID);
        }

        m_Offset += m_BlindingExcess;
        m_BlindingExcess = -m_BlindingExcess;

        // Don't store the generated nonce for the kernel multisig. Instead - store the raw random, from which the nonce is derived using kdf.
        NoLeak<Hash::Value> hvRandom;
        if (!m_Tx.GetParameter(TxParameterID::MyNonce, hvRandom.V, m_SubTxID))
        {
            ECC::GenRandom(hvRandom.V);
            m_Tx.SetParameter(TxParameterID::MyNonce, hvRandom.V, false, m_SubTxID);
        }

        m_Tx.GetWalletDB()->get_MasterKdf()->DeriveKey(m_MultiSig.m_Nonce, hvRandom.V);
    }

    void BaseTxBuilder::SignPartial()
    {
        // create signature
        Point::Native totalPublicExcess = GetPublicExcess();
        totalPublicExcess += m_PeerPublicExcess;
        m_Kernel->m_Commitment = totalPublicExcess;

        m_Kernel->get_Hash(m_Message, m_PeerLockImage.get());
        m_MultiSig.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        
        
        m_MultiSig.SignPartial(m_PartialSignature, m_Message, m_BlindingExcess);

        StoreKernelID();
    }

    void BaseTxBuilder::FinalizeSignature()
    {
        // final signature
        m_Kernel->m_Signature.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        m_Kernel->m_Signature.m_k = m_PartialSignature + m_PeerSignature;
        
        StoreKernelID();
    }

    Transaction::Ptr BaseTxBuilder::CreateTransaction()
    {
        assert(m_Kernel);
        LOG_INFO() << m_Tx.GetTxID() << " Transaction created. Kernel: " << GetKernelIDString();

        // create transaction
        auto transaction = make_shared<Transaction>();
        transaction->m_vKernels.push_back(move(m_Kernel));
        transaction->m_Offset = m_Offset + m_PeerOffset;
        transaction->m_vInputs = move(m_Inputs);
        transaction->m_vOutputs = move(m_Outputs);
        move(m_PeerInputs.begin(), m_PeerInputs.end(), back_inserter(transaction->m_vInputs));
        move(m_PeerOutputs.begin(), m_PeerOutputs.end(), back_inserter(transaction->m_vOutputs));

        transaction->Normalize();

        return transaction;
    }

    void BaseTxBuilder::StoreKernelID()
    {
        assert(m_Kernel);
        Merkle::Hash kernelID;
        m_Kernel->get_ID(kernelID, m_PeerLockImage.get());

        m_Tx.SetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
    }

    bool BaseTxBuilder::IsPeerSignatureValid() const
    {
        Signature peerSig;
        peerSig.m_NoncePub = m_MultiSig.m_NoncePub;
        peerSig.m_k = m_PeerSignature;
        return peerSig.IsValidPartial(m_Message, m_PeerPublicNonce, m_PeerPublicExcess);
    }

    Point::Native BaseTxBuilder::GetPublicExcess() const
    {
        return Context::get().G * m_BlindingExcess;
    }

    Point::Native BaseTxBuilder::GetPublicNonce() const
    {
        return Context::get().G * m_MultiSig.m_Nonce;
    }

    bool BaseTxBuilder::GetPeerPublicExcessAndNonce()
    {
        return m_Tx.GetParameter(TxParameterID::PeerPublicExcess, m_PeerPublicExcess, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::PeerPublicNonce, m_PeerPublicNonce, m_SubTxID);
    }

    bool BaseTxBuilder::GetPeerSignature()
    {
        if (m_Tx.GetParameter(TxParameterID::PeerSignature, m_PeerSignature, m_SubTxID))
        {
            LOG_DEBUG() << m_Tx.GetTxID() << " Received PeerSig:\t" << Scalar(m_PeerSignature);
            return true;
        }

        return false;
    }

    bool BaseTxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::MinHeight, m_MinHeight, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::MaxHeight, m_MaxHeight, m_SubTxID);
        return m_Tx.GetParameter(TxParameterID::BlindingExcess, m_BlindingExcess, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::Offset, m_Offset, m_SubTxID);
    }

    bool BaseTxBuilder::GetPeerInputsAndOutputs()
    {
        // used temporary vars to avoid non-short circuit evaluation
        bool hasInputs = m_Tx.GetParameter(TxParameterID::PeerInputs, m_PeerInputs, m_SubTxID);
        bool hasOutputs = (m_Tx.GetParameter(TxParameterID::PeerOutputs, m_PeerOutputs, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID));
        return hasInputs || hasOutputs;
    }

    Amount BaseTxBuilder::GetAmount() const
    {
        return std::accumulate(m_AmountList.begin(), m_AmountList.end(), 0ULL);
    }

    const AmountList& BaseTxBuilder::GetAmountList() const
    {
        return m_AmountList;
    }

    Amount BaseTxBuilder::GetFee() const
    {
        return m_Fee;
    }

    Height BaseTxBuilder::GetMinHeight() const
    {
        return m_MinHeight;
    }

    Height BaseTxBuilder::GetMaxHeight() const
    {
        return m_MaxHeight;
    }

    const vector<Input::Ptr>& BaseTxBuilder::GetInputs() const
    {
        return m_Inputs;
    }

    const vector<Output::Ptr>& BaseTxBuilder::GetOutputs() const
    {
        return m_Outputs;
    }

    const Scalar::Native& BaseTxBuilder::GetOffset() const
    {
        return m_Offset;
    }

    const Scalar::Native& BaseTxBuilder::GetPartialSignature() const
    {
        return m_PartialSignature;
    }

    const TxKernel& BaseTxBuilder::GetKernel() const
    {
        assert(m_Kernel);
        return *m_Kernel;
    }

    string BaseTxBuilder::GetKernelIDString() const
    {
        Merkle::Hash kernelID;
        m_Kernel->get_ID(kernelID, m_PeerLockImage.get());

        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }

    Hash::Value BaseTxBuilder::GetLockImage() const
    {
        Hash::Value lockImage(Zero);
        if (m_Kernel->m_pHashLock)
        {
            Hash::Processor() << m_Kernel->m_pHashLock->m_Preimage >> lockImage;
        }

        return lockImage;
    }
}
