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
#include "aissue_tx_builder.h"
#include "assets_kdf_utils.h"
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "wallet/core/strings_resources.h"
#include <memory>
#include <numeric>

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    AssetIssueTxBuilder::AssetIssueTxBuilder(bool issue, BaseTransaction& tx, SubTxID subTxID)
        : m_Tx{tx}
        , m_SubTxID(subTxID)
        , m_assetOwnerIdx(0)
        , m_assetOwnerId(0UL)
        , m_issue(issue)
        , m_AmountList{0}
        , m_Fee(0)
        , m_ChangeBeam(0)
        , m_ChangeAsset(0)
        , m_MinHeight(0)
        , m_MaxHeight(MaxHeight)
    {
        auto masterKdf = m_Tx.get_MasterKdfStrict(); // can throw

        m_Fee = m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Fee, m_SubTxID);
        if (!m_Tx.GetParameter(TxParameterID::AmountList, m_AmountList, m_SubTxID))
        {
            m_AmountList = AmountList{m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Amount, m_SubTxID)};
        }

        m_assetOwnerIdx = m_Tx.GetMandatoryParameter<Key::Index>(TxParameterID::AssetOwnerIdx);
        if (m_assetOwnerIdx == Asset::s_InvalidOwnerIdx)
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoAssetId);
        }

        m_assetOwnerId = GetAssetOwnerID(masterKdf, m_assetOwnerIdx);
        if (m_assetOwnerId == Zero)
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoAssetId);
        }
    }

    bool AssetIssueTxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::ChangeAsset,m_ChangeAsset, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::Inputs,     m_Inputs,      m_SubTxID);
        m_Tx.GetParameter(TxParameterID::Outputs,    m_Outputs,     m_SubTxID);
        bool hasICoins = m_Tx.GetParameter(TxParameterID::InputCoins, m_InputCoins,  m_SubTxID);
        bool hasOCoins = m_Tx.GetParameter(TxParameterID::OutputCoins,m_OutputCoins, m_SubTxID);

        if (!m_Tx.GetParameter(TxParameterID::MinHeight, m_MinHeight, m_SubTxID))
        {
            m_MinHeight = m_Tx.GetWalletDB()->getCurrentHeight();
            m_Tx.SetParameter(TxParameterID::MinHeight, m_MinHeight, m_SubTxID);
        }

        if (!m_Tx.GetParameter(TxParameterID::MaxHeight, m_MaxHeight, m_SubTxID))
        {
            Height lifetime = kDefaultTxLifetime;
            m_Tx.GetParameter(TxParameterID::Lifetime, lifetime,m_SubTxID);

            m_MaxHeight = m_MinHeight + lifetime;
            m_Tx.SetParameter(TxParameterID::MaxHeight, m_MaxHeight, m_SubTxID);
        }

        m_Tx.GetParameter(TxParameterID::Offset, m_Offset, m_SubTxID);
        return hasICoins || hasOCoins;
    }

    void AssetIssueTxBuilder::CreateInputs()
    {
        if (GetInputs() || GetInputCoins().empty())
        {
            return;
        }

        Key::IKdf::Ptr pMasterKdf = m_Tx.get_MasterKdfStrict();

        m_Inputs.reserve(m_InputCoins.size());
        for (const CoinID& cid : m_InputCoins)
        {
            m_Inputs.emplace_back();
            m_Inputs.back() = std::make_unique<Input>();

            ECC::Scalar::Native sk;
            CoinID::Worker(cid).Create(sk, m_Inputs.back()->m_Commitment, *cid.get_ChildKdf(pMasterKdf));

            m_Offset += sk;
        }

        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);
    }

    bool AssetIssueTxBuilder::LoadKernel()
    {
        if (m_Tx.GetParameter(TxParameterID::Kernel, m_Kernel, m_SubTxID))
        {
            GetInitialTxParams();
            return true;
        }
        return false;
    }

    Transaction::Ptr AssetIssueTxBuilder::CreateTransaction()
    {
        // Don't display in log infinite max height
        if (m_Kernel->m_Height.m_Max == MaxHeight)
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << m_Kernel->m_Height.m_Min;
        }
        else
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << m_Kernel->m_Height.m_Min
                << ", max height: " << m_Kernel->m_Height.m_Max;
        }

        auto tx = make_shared<Transaction>();

        tx->m_vInputs  = std::move(m_Inputs);
        tx->m_vOutputs = std::move(m_Outputs);
        tx->m_vKernels.push_back(std::move(m_Kernel));

        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);
        tx->m_Offset = m_Offset;
        tx->Normalize();

#ifdef DEBUG
        beam::Transaction::Context::Params pars;
        beam::Transaction::Context ctx(pars);
        ctx.m_Height.m_Min = m_MinHeight;
        assert(tx->IsValid(ctx));
#endif

        return tx;
    }

    Amount AssetIssueTxBuilder::GetTransactionAmount() const
    {
        return std::accumulate(m_AmountList.begin(), m_AmountList.end(), 0ULL);
    }

    Amount AssetIssueTxBuilder::GetAmountAsset() const
    {
        return m_issue ? 0 : std::accumulate(m_AmountList.begin(), m_AmountList.end(), 0ULL);
    }

    const AmountList& AssetIssueTxBuilder::GetAmountList() const
    {
        return m_AmountList;
    }

    Height AssetIssueTxBuilder::GetMinHeight() const
    {
        return m_MinHeight;
    }

    const std::vector<Coin::ID>& AssetIssueTxBuilder::GetInputCoins() const
    {
        return m_InputCoins;
    }

    const std::vector<Coin::ID>& AssetIssueTxBuilder::GetOutputCoins() const
    {
        return m_OutputCoins;
    }

    void AssetIssueTxBuilder::AddChange()
    {
        if (m_ChangeBeam)
        {
            GenerateBeamCoin(m_ChangeBeam, true);
        }

        if (m_ChangeAsset)
        {
            GenerateAssetCoin(m_ChangeAsset, true);
        }

         m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
    }

    Amount AssetIssueTxBuilder::GetFee() const
    {
        return m_Fee;
    }

    Key::Index AssetIssueTxBuilder::GetAssetOwnerIdx() const
    {
        return m_assetOwnerIdx;
    }

     Asset::ID AssetIssueTxBuilder::GetAssetId() const
     {
         return m_Tx.GetMandatoryParameter<Asset::ID>(TxParameterID::AssetID, m_SubTxID);;
     }

     PeerID AssetIssueTxBuilder::GetAssetOwnerId() const
     {
        return m_assetOwnerId;
     }

    string AssetIssueTxBuilder::GetKernelIDString() const {
        Merkle::Hash kernelID;
        m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }

    bool AssetIssueTxBuilder::GetInputs()
    {
        return m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs, m_SubTxID);
    }

    bool AssetIssueTxBuilder::GetOutputs()
    {
        return m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs, m_SubTxID);
    }

    void AssetIssueTxBuilder::SelectInputCoins()
    {
        CoinIDList preselIDs;
        vector<Coin> coins;

        Amount preselAmountBeam  = 0;
        Amount preselAmountAsset = 0;
        const auto assetId = GetAssetId();

        auto isAssetCoin = [&] (const Coin& coin) {
            return coin.m_ID.m_AssetID == assetId;
        };

        if (m_Tx.GetParameter(TxParameterID::PreselectedCoins, preselIDs, m_SubTxID))
        {
            if (!preselIDs.empty())
            {
                coins = m_Tx.GetWalletDB()->getCoinsByID(preselIDs);
                for (auto &coin : coins)
                {
                    isAssetCoin(coin) ? preselAmountAsset += coin.getAmount() : preselAmountBeam += coin.getAmount();
                    coin.m_spentTxId = m_Tx.GetTxID();
                }
                m_Tx.GetWalletDB()->saveCoins(coins);
            }
        }

        Amount amountBeamWithFee = m_Fee; // for issue/consue BEAMs are only used for fees
        if (preselAmountBeam < amountBeamWithFee)
        {
            auto selectedCoins = m_Tx.GetWalletDB()->selectCoins(amountBeamWithFee - preselAmountBeam, Zero);
            if (selectedCoins.empty())
            {
                storage::Totals totals(*m_Tx.GetWalletDB());
                const auto& beamTotals = totals.GetTotals(Zero);
                LOG_ERROR() << m_Tx.GetTxID() << "[" << m_SubTxID << "]" << " You only have " << PrintableAmount(beamTotals.Avail);
                throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoInputs);
            }
            copy(selectedCoins.begin(), selectedCoins.end(), back_inserter(coins));
        }

        Amount amountAsset = GetAmountAsset();
        if (preselAmountAsset < amountAsset)
        {
            auto selectedCoins = m_Tx.GetWalletDB()->selectCoins(amountAsset - preselAmountAsset, assetId);
            if (selectedCoins.empty())
            {
                storage::Totals totals(*m_Tx.GetWalletDB());
                const auto& assetTotals(totals.GetTotals(assetId));
                LOG_ERROR() << m_Tx.GetTxID() << "[" << m_SubTxID << "]" << " You only have " << PrintableAmount(assetTotals.Avail, false, kAmountASSET, kAmountAGROTH);
                throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoInputs);
            }
            copy(selectedCoins.begin(), selectedCoins.end(), back_inserter(coins));
        }

        m_InputCoins.reserve(coins.size());
        Amount totalBeam = 0;
        Amount totalAsset = 0;

        for (auto& coin : coins)
        {
            coin.m_spentTxId = m_Tx.GetTxID();
            isAssetCoin(coin) ? totalAsset += coin.m_ID.m_Value : totalBeam += coin.m_ID.m_Value;
            m_InputCoins.push_back(coin.m_ID);
        }

        m_ChangeBeam  = totalBeam  - amountBeamWithFee;
        m_ChangeAsset = totalAsset - amountAsset;

        m_Tx.SetParameter(TxParameterID::ChangeBeam,  m_ChangeBeam,  false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::ChangeAsset, m_ChangeAsset, false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::InputCoins,  m_InputCoins,  false, m_SubTxID);
        m_Tx.GetWalletDB()->saveCoins(coins);
    }

    void AssetIssueTxBuilder::GenerateAssetCoin(Amount amount, bool change)
    {
        const auto assetId = GetAssetId();
        Coin newUtxo(amount, change ? Key::Type::Change : Key::Type::Regular, assetId);
        newUtxo.m_createTxId = m_Tx.GetTxID();
        m_Tx.GetWalletDB()->storeCoin(newUtxo);
        m_OutputCoins.push_back(newUtxo.m_ID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
        LOG_INFO() << m_Tx.GetTxID() << " Creating ASSET coin" << (change ? " (change):" : ":")
                   << PrintableAmount(amount, false, kAmountASSET, kAmountAGROTH)
                   << ", asset id " << assetId
                   << ", id " << newUtxo.toStringID();
    }

    void AssetIssueTxBuilder::GenerateBeamCoin(Amount amount, bool change)
    {
        Coin newUtxo(amount, change ? Key::Type::Change : Key::Type::Regular);
        newUtxo.m_createTxId = m_Tx.GetTxID();
        m_Tx.GetWalletDB()->storeCoin(newUtxo);
        m_OutputCoins.push_back(newUtxo.m_ID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
        LOG_INFO() << m_Tx.GetTxID() << " Creating BEAM coin" << (change ? " (change):" : ":")
                   << PrintableAmount(amount)
                   << ", id " << newUtxo.toStringID();
    }

    void AssetIssueTxBuilder::CreateOutputs()
    {
        if (GetOutputs())
        {
            // if we already have outputs, nothing to do here
            return;
        }

        if (GetOutputCoins().empty())
        {
            // Output coins should be generated already
            return;
        }

        Key::IKdf::Ptr pMasterKdf = m_Tx.get_MasterKdfStrict();

        m_Outputs.reserve(m_OutputCoins.size());
        for (const CoinID& cid : m_OutputCoins)
        {
            m_Outputs.emplace_back();
            m_Outputs.back().reset(new Output);

            Scalar::Native sk;
            m_Outputs.back()->Create(m_MinHeight, sk, *cid.get_ChildKdf(pMasterKdf), cid, *pMasterKdf);
            sk = -sk;
            m_Offset += sk;
        }
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false, m_SubTxID);
    }

    const Merkle::Hash& AssetIssueTxBuilder::GetKernelID() const
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

    void AssetIssueTxBuilder::MakeKernel()
    {
        static_assert(std::is_same<decltype(m_Kernel->m_Value), int64_t>::value,
                      "If this fails please update value in the ICAmountTooBig's message and typecheck in this assert");

        // Note, m_Kernel is still nullptr, do not don't anything except typecheks here
        if (GetTransactionAmount() > (Amount)std::numeric_limits<decltype(m_Kernel->m_Value)>::max())
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), ICAmountTooBig);
        }

        assert(!m_Kernel);
        m_Kernel = make_unique<TxKernelAssetEmit>();
        m_Kernel->m_Fee          = m_Fee;
        m_Kernel->m_Height.m_Min = GetMinHeight();
        m_Kernel->m_Height.m_Max = m_MaxHeight;
        m_Kernel->m_Commitment   = Zero;
        m_Kernel->m_AssetID      = GetAssetId();
        m_Kernel->m_Owner        = m_assetOwnerId;
        m_Kernel->m_Value        = m_issue ? GetTransactionAmount() : -static_cast<AmountSigned>(GetTransactionAmount());

        auto masterKdf = m_Tx.get_MasterKdfStrict();
        m_Offset = SignAssetKernel(masterKdf, m_InputCoins, m_OutputCoins, m_assetOwnerIdx, *m_Kernel);
        const Merkle::Hash& kernelID = m_Kernel->m_Internal.m_ID;

        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::Kernel, m_Kernel, m_SubTxID);
    }
}

