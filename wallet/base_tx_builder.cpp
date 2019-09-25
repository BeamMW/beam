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
        , m_Change{ 0 }
        , m_Lifetime{ kDefaultTxLifetime }
        , m_MinHeight{ 0 }
        , m_MaxHeight{ MaxHeight }
        , m_PeerMaxHeight{ MaxHeight }
    {
        if (m_AmountList.empty())
        {
            m_Tx.GetParameter(TxParameterID::AmountList, m_AmountList, m_SubTxID);
        }
        if (m_Fee == 0)
        {
            m_Tx.GetParameter(TxParameterID::Fee, m_Fee, m_SubTxID);
        }
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
            m_Tx.GetWalletDB()->saveCoins(coins);
        }

        Amount amountWithFee = GetAmount() + GetFee();
        if (preselectedAmount < amountWithFee)
        {
            auto selectedCoins = m_Tx.GetWalletDB()->selectCoins(amountWithFee - preselectedAmount);
            copy(selectedCoins.begin(), selectedCoins.end(), back_inserter(coins));
        }

        if (coins.empty())
        {
            storage::Totals totals(*m_Tx.GetWalletDB());

            LOG_ERROR() << m_Tx.GetTxID() << "[" << m_SubTxID << "]" << " You only have " << PrintableAmount(totals.Avail);
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoInputs);
        }

        m_InputCoins.reserve(coins.size());

        Amount total = 0;
        for (auto& coin : coins)
        {
            coin.m_spentTxId = m_Tx.GetTxID();
            total += coin.m_ID.m_Value;
            m_InputCoins.push_back(coin.m_ID);
        }

        m_Change += total - amountWithFee;

        m_Tx.SetParameter(TxParameterID::Change, m_Change, false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::InputCoins, m_InputCoins, false, m_SubTxID);

        m_Tx.GetWalletDB()->saveCoins(coins);
    }

    void BaseTxBuilder::AddChange()
    {
        if (m_Change == 0)
        {
            return;
        }

        GenerateNewCoin(m_Change, true);
    }

    void BaseTxBuilder::GenerateNewCoin(Amount amount, bool bChange)
    {
        Coin newUtxo{ amount };
        newUtxo.m_createTxId = m_Tx.GetTxID();
        if (bChange)
        {
            newUtxo.m_ID.m_Type = Key::Type::Change;
        }
        m_Tx.GetWalletDB()->storeCoin(newUtxo);
        m_OutputCoins.push_back(newUtxo.m_ID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
    }

    bool BaseTxBuilder::CreateOutputs()
    {
        if (GetOutputs() || GetOutputCoins().empty())
        {
            return false;
        }

        auto thisHolder = shared_from_this();
        auto txHolder = m_Tx.shared_from_this(); // increment use counter of tx object. We use it to avoid tx object desctruction during Update call.
        m_Tx.GetAsyncAcontext().OnAsyncStarted();
        m_Tx.GetKeyKeeper()->GenerateOutputs(m_MinHeight, m_OutputCoins,
            [thisHolder, this, txHolder](auto&& result)
            {
                m_Outputs = move(result);
                FinalizeOutputs();
                m_Tx.Update(); // may complete tranasction 
                m_Tx.GetAsyncAcontext().OnAsyncFinished();
            },
            [thisHolder, this, txHolder](const exception&)
            {
                //m_Tx.Update();
                m_Tx.GetAsyncAcontext().OnAsyncFinished();
            });
        return true;// true if async
    }

    bool BaseTxBuilder::FinalizeOutputs()
    {
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false, m_SubTxID);

        // TODO: check transaction size here

        return true;
    }

    bool BaseTxBuilder::CreateInputs()
    {
        if (GetInputs() || GetInputCoins().empty())
        {
            return false;
        }
        //auto thisHolder = shared_from_this();
        //m_Tx.GetKeyKeeper()->GenerateKey(m_InputCoins, true,
        //    [thisHolder, this](const auto & result)
        //    {
        //        m_Inputs.reserve(result.size());
        //        for (const auto& commitment : result)
        //        {
        //            auto& input = m_Inputs.emplace_back(make_unique<Input>());
        //            input->m_Commitment = commitment;
        //        }
        //        FinalizeInputs();
        //        //m_Tx.Update();
        //    },
        //    [thisHolder, this](const exception&)
        //    {
        //        //m_Tx.Update();
        //    });
        auto commitments = m_Tx.GetKeyKeeper()->GeneratePublicKeysSync(m_InputCoins, true);
        m_Inputs.reserve(commitments.size());
        for (const auto& commitment : commitments)
        {
            auto& input = m_Inputs.emplace_back(make_unique<Input>());
            input->m_Commitment = commitment;
        }
        FinalizeInputs();
        return false; // true if async operation has run
    }

    void BaseTxBuilder::FinalizeInputs()
    {
        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);
    }

    void BaseTxBuilder::CreateKernel()
    {
        // create kernel
        assert(!m_Kernel);
        m_Kernel = make_unique<TxKernel>();
        m_Kernel->m_Fee = GetFee();
        m_Kernel->m_Height.m_Min = GetMinHeight();
        m_Kernel->m_Height.m_Max = GetMaxHeight();
        m_Kernel->m_Commitment = Zero;

        m_Tx.SetParameter(TxParameterID::MaxHeight, GetMaxHeight(), m_SubTxID);

        // load kernel's extra data
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
    }

    void BaseTxBuilder::GenerateOffset()
    {
        m_Offset.GenRandomNnz();
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);
    }

    void BaseTxBuilder::GenerateNonce()
    {
        // Don't store the generated nonce for the kernel multisig. Instead - store the raw random, from which the nonce is derived using kdf.
        if (!m_Tx.GetParameter(TxParameterID::NonceSlot, m_NonceSlot, m_SubTxID))
        {
            m_NonceSlot = m_Tx.GetKeyKeeper()->AllocateNonceSlot();
            m_Tx.SetParameter(TxParameterID::NonceSlot, m_NonceSlot, false, m_SubTxID);
        }
        
        if (!m_Tx.GetParameter(TxParameterID::PublicNonce, m_PublicNonce, m_SubTxID))
        {
            auto pt = m_Tx.GetKeyKeeper()->GenerateNonceSync(m_NonceSlot);
            m_PublicNonce.Import(pt);
            m_Tx.SetParameter(TxParameterID::PublicNonce, m_PublicNonce, false, m_SubTxID);
        }
    }

    Point::Native BaseTxBuilder::GetPublicExcess() const
    {
        // PublicExcess = Sum(inputs) - Sum(outputs) - offset * G - (Sum(input amounts) - Sum(output amounts)) * H
        Point::Native publicAmount = Zero;
        Amount amount = 0;
        for (const auto& cid : m_InputCoins)
        {
            amount += cid.m_Value;
        }
        AmountBig::AddTo(publicAmount, amount);
        amount = 0;
        publicAmount = -publicAmount;
        for (const auto& cid : m_OutputCoins)
        {
            amount += cid.m_Value;
        }
        AmountBig::AddTo(publicAmount, amount);

        Point::Native publicExcess = Context::get().G * m_Offset;
        {
            Point::Native commitment;

            for (const auto& output : m_Outputs)
            {
                if (commitment.Import(output->m_Commitment))
                {
                    publicExcess += commitment;
                }
            }

            publicExcess = -publicExcess;
            for (const auto& input : m_Inputs)
            {
                if (commitment.Import(input->m_Commitment))
                {
                    publicExcess += commitment;
                }
            }
        }
        publicExcess += publicAmount;
        return publicExcess;
    }

    Point::Native BaseTxBuilder::GetPublicNonce() const
    {
        return m_PublicNonce;
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
            LOG_DEBUG() << m_Tx.GetTxID() << "[" << m_SubTxID << "]" << " Received PeerSig:\t" << Scalar(m_PeerSignature);
            return true;
        }

        return false;
    }

    bool BaseTxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::InputCoins, m_InputCoins, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::OutputCoins, m_OutputCoins, m_SubTxID);

        if (!m_Tx.GetParameter(TxParameterID::MinHeight, m_MinHeight, m_SubTxID))
        {
            // adjust min height, this allows create transaction when node is out of sync
            auto currentHeight = m_Tx.GetWalletDB()->getCurrentHeight();
            m_MinHeight = currentHeight;
            m_Tx.SetParameter(TxParameterID::MinHeight, m_MinHeight, m_SubTxID);

            Height responseTime = 0;
            if (m_Tx.GetParameter(TxParameterID::PeerResponseTime, responseTime, m_SubTxID))
            {
                // adjust response height, if min height din not set then then it should be equal to responce time
                m_Tx.SetParameter(TxParameterID::PeerResponseHeight, responseTime + currentHeight, m_SubTxID);
            }

        }
        m_Tx.GetParameter(TxParameterID::Lifetime, m_Lifetime, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::PeerMaxHeight, m_PeerMaxHeight, m_SubTxID);

        CheckMinimumFee();

        return m_Tx.GetParameter(TxParameterID::Offset, m_Offset, m_SubTxID);
    }

    bool BaseTxBuilder::GetInputs()
    {
        return m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs, m_SubTxID);
    }

    bool BaseTxBuilder::GetOutputs()
    {
        return m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs, m_SubTxID);
    }

    bool BaseTxBuilder::GetPeerInputsAndOutputs()
    {
        // used temporary vars to avoid non-short circuit evaluation
        bool hasInputs = m_Tx.GetParameter(TxParameterID::PeerInputs, m_PeerInputs, m_SubTxID);
        bool hasOutputs = (m_Tx.GetParameter(TxParameterID::PeerOutputs, m_PeerOutputs, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID));
        return hasInputs || hasOutputs;
    }

    void BaseTxBuilder::SignPartial()
    {
        // create signature
        Point::Native totalPublicExcess = GetPublicExcess();
        totalPublicExcess += m_PeerPublicExcess;
        m_Kernel->m_Commitment = totalPublicExcess;

        m_Kernel->get_Hash(m_Message, m_PeerLockImage.get());

        KernelParameters kernelParameters;
        kernelParameters.fee = m_Fee;
        kernelParameters.height = { GetMinHeight(), GetMaxHeight() };
        kernelParameters.commitment = totalPublicExcess;
        if (m_PeerLockImage)
        {
            *kernelParameters.lockImage = *m_PeerLockImage;
        }
        if (m_Kernel->m_pHashLock)
        {
            *kernelParameters.hashLock = *m_Kernel->m_pHashLock;
        }

        m_PartialSignature = m_Tx.GetKeyKeeper()->SignSync(m_InputCoins, m_OutputCoins, m_Offset, m_NonceSlot, kernelParameters, GetPublicNonce() + m_PeerPublicNonce);

        StoreKernelID();
    }

    void BaseTxBuilder::FinalizeSignature()
    {
        // final signature
        m_Kernel->m_Signature.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        m_Kernel->m_Signature.m_k = m_PartialSignature + m_PeerSignature;

        StoreKernelID();
        m_Tx.SetParameter(TxParameterID::Kernel, m_Kernel, m_SubTxID);
    }

    bool BaseTxBuilder::LoadKernel()
    {
        if (m_Tx.GetParameter(TxParameterID::Kernel, m_Kernel, m_SubTxID))
        {
            GetInitialTxParams();
            return true;
        }
        return false;
    }

    bool BaseTxBuilder::HasKernelID() const
    {
        Merkle::Hash kernelID;
        return m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
    }

    Transaction::Ptr BaseTxBuilder::CreateTransaction()
    {
        assert(m_Kernel);
        // Don't display in log infinite max height
        if (m_Kernel->m_Height.m_Max == MaxHeight)
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << " min height: " << m_Kernel->m_Height.m_Min;
        }
        else
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << " min height: " << m_Kernel->m_Height.m_Min
                << " max height: " << m_Kernel->m_Height.m_Max;
        }

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

    bool BaseTxBuilder::IsPeerSignatureValid() const
    {
        Signature peerSig;
        peerSig.m_NoncePub = m_PeerPublicNonce + GetPublicNonce();
        peerSig.m_k = m_PeerSignature;
        return peerSig.IsValidPartial(m_Message, m_PeerPublicNonce, m_PeerPublicExcess);
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

    Height BaseTxBuilder::GetLifetime() const
    {
        return m_Lifetime;
    }

    Height BaseTxBuilder::GetMinHeight() const
    {
        return m_MinHeight;
    }

    Height BaseTxBuilder::GetMaxHeight() const
    {
        if (m_MaxHeight == MaxHeight)
        {
            return m_MinHeight + m_Lifetime;
        }
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

    Hash::Value BaseTxBuilder::GetLockImage() const
    {
        Hash::Value lockImage(Zero);
        if (m_Kernel->m_pHashLock)
        {
            Hash::Processor() << m_Kernel->m_pHashLock->m_Preimage >> lockImage;
        }

        return lockImage;
    }

    const Merkle::Hash& BaseTxBuilder::GetKernelID() const
    {
        if (!m_KernelID)
        {
            Merkle::Hash kernelID;
            if (m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID))
            {
                m_KernelID = kernelID;
            }
            else
            {
                assert(false && "KernelID is not stored");
            }

        }
        return *m_KernelID;
    }

    void BaseTxBuilder::StoreKernelID()
    {
        assert(m_Kernel);
        Merkle::Hash kernelID;
        m_Kernel->get_ID(kernelID, m_PeerLockImage.get());

        m_Tx.SetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
    }

    string BaseTxBuilder::GetKernelIDString() const
    {
        Merkle::Hash kernelID;
        m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }

    SubTxID BaseTxBuilder::GetSubTxID() const
    {
        return m_SubTxID;
    }

    bool BaseTxBuilder::UpdateMaxHeight()
    {
        Merkle::Hash kernelId;
        if (!m_Tx.GetParameter(TxParameterID::MaxHeight, m_MaxHeight, m_SubTxID) &&
            !m_Tx.GetParameter(TxParameterID::KernelID, kernelId, m_SubTxID))
        {
            bool isInitiator = m_Tx.IsInitiator();
            bool hasPeerMaxHeight = m_PeerMaxHeight < MaxHeight;
            if (!isInitiator)
            {
                if (m_Tx.GetParameter(TxParameterID::Lifetime, m_Lifetime, m_SubTxID))
                {
                    Block::SystemState::Full state;
                    if (m_Tx.GetTip(state))
                    {
                        m_MaxHeight = state.m_Height + m_Lifetime;
                    }
                }
                else if (hasPeerMaxHeight)
                {
                    m_MaxHeight = m_PeerMaxHeight;
                }
            }
            else if (hasPeerMaxHeight)
            {
                if (IsAcceptableMaxHeight())
                {
                    m_MaxHeight = m_PeerMaxHeight;
                }
                else
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool BaseTxBuilder::IsAcceptableMaxHeight() const
    {
        Height lifetime = 0;
        Height peerResponceHeight = 0;
        if (!m_Tx.GetParameter(TxParameterID::Lifetime, lifetime, m_SubTxID)
            || !m_Tx.GetParameter(TxParameterID::PeerResponseHeight, peerResponceHeight, m_SubTxID))
        {
            // possible situation during update from older version
            return true;
        }
        Height maxAcceptableHeight = lifetime + peerResponceHeight;
        return m_PeerMaxHeight < MaxHeight&& m_PeerMaxHeight <= maxAcceptableHeight;
    }

    const std::vector<Coin::ID>& BaseTxBuilder::GetInputCoins() const
    {
        return m_InputCoins;
    }

    const std::vector<Coin::ID>& BaseTxBuilder::GetOutputCoins() const
    {
        return m_OutputCoins;
    }

    Amount BaseTxBuilder::GetMinimumFee() const
    {
        auto numberOfOutputs = GetAmountList().size() + 1; // +1 for possible change to simplify logic TODO: need to review

        return wallet::GetMinimumFee(numberOfOutputs);
    }

    void BaseTxBuilder::CheckMinimumFee()
    {
        // after 1st fork fee should be >= minimal fee
        if (Rules::get().pForks[1].m_Height <= GetMinHeight())
        {
            auto minimalFee = GetMinimumFee();
            Amount userFee = 0;
            if (m_Tx.GetParameter(TxParameterID::Fee, userFee, m_SubTxID))
            {
                if (userFee < minimalFee)
                {
                    stringstream ss;
                    ss << "The minimum fee must be: " << minimalFee << " .";
                    throw TransactionFailedException(false, TxFailureReason::FeeIsTooSmall, ss.str().c_str());
                }
            }
        }
    }
}
