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
#include "aregister_tx_builder.h"
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "wallet/core/strings_resources.h"
#include <numeric>

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    AssetRegisterTxBuilder::AssetRegisterTxBuilder(bool issue, BaseTransaction& tx, SubTxID subTxID, IPrivateKeyKeeper::Ptr keyKeeper)
        : m_Tx{tx}
        , m_keyKeeper(std::move(keyKeeper))
        , m_SubTxID(subTxID)
        , m_assetOwnerIdx(0)
        , m_assetOwnerId(Zero)
        , m_issue(issue)
        , m_Fee(0)
        , m_ChangeBeam(0)
        , m_MinHeight(0)
        , m_MaxHeight(MaxHeight)
        , m_Offset(Zero)
    {
        if (!m_keyKeeper.get())
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoKeyKeeper);
        }

        m_Fee            =  m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Fee, m_SubTxID);
        m_assetOwnerIdx  =  m_Tx.GetMandatoryParameter<Key::Index>(TxParameterID::AssetOwnerIdx);
        m_assetOwnerId   =  m_keyKeeper->GetAssetOwnerID(m_assetOwnerIdx);

        if (m_assetOwnerIdx == 0 || m_assetOwnerId == Zero)
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoAssetId);
        }
    }

    bool AssetRegisterTxBuilder::CreateInputs()
    {
        if (GetInputs() || GetInputCoins().empty())
        {
            return false;
        }

        const auto& [commitments, commitmentsOffset] = m_Tx.GetKeyKeeper()->GeneratePublicKeysSyncEx(m_InputCoins, true, Zero);
        m_Offset += commitmentsOffset;
        m_Inputs.reserve(commitments.size());
        for (const auto& commitment : commitments)
        {
            auto& input = m_Inputs.emplace_back(make_unique<Input>());
            input->m_Commitment = commitment;
        }

        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);
        return false; // true if async operation has run
    }

    bool AssetRegisterTxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::Inputs,     m_Inputs,      m_SubTxID);
        m_Tx.GetParameter(TxParameterID::Outputs,    m_Outputs,     m_SubTxID);
        m_Tx.GetParameter(TxParameterID::InputCoins, m_InputCoins,  m_SubTxID);
        m_Tx.GetParameter(TxParameterID::OutputCoins,m_OutputCoins, m_SubTxID);

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

        return m_Tx.GetParameter(TxParameterID::Offset, m_Offset, m_SubTxID);
    }

    bool AssetRegisterTxBuilder::LoadKernel()
    {
        if (m_Tx.GetParameter(TxParameterID::Kernel, m_regKernel, m_SubTxID))
        {
            GetInitialTxParams();
            return true;
        }
        return false;
    }

    Transaction::Ptr AssetRegisterTxBuilder::CreateTransaction()
    {
        // Don't display in log infinite max height
        if (m_regKernel->m_Height.m_Max == MaxHeight)
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << m_regKernel->m_Height.m_Min;
        }
        else
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << m_regKernel->m_Height.m_Min
                << ", max height: " << m_regKernel->m_Height.m_Max;
        }

        auto tx = make_shared<Transaction>();

        tx->m_vInputs  = std::move(m_Inputs);
        tx->m_vOutputs = std::move(m_Outputs);
        tx->m_vKernels.push_back(std::move(m_regKernel));

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

    Height AssetRegisterTxBuilder::GetMinHeight() const
    {
        return m_MinHeight;
    }

    const std::vector<Coin::ID>& AssetRegisterTxBuilder::GetInputCoins() const
    {
        return m_InputCoins;
    }

    const std::vector<Coin::ID>& AssetRegisterTxBuilder::GetOutputCoins() const
    {
        return m_OutputCoins;
    }

    void AssetRegisterTxBuilder::AddChange()
    {
        if (m_ChangeBeam)
        {
            GenerateBeamCoin(m_ChangeBeam, true);
        }

         m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
    }

    Amount AssetRegisterTxBuilder::GetFee() const
    {
        return m_Fee;
    }

    Key::Index AssetRegisterTxBuilder::GetAssetOwnerIdx() const
    {
        return m_assetOwnerIdx;
    }

     PeerID AssetRegisterTxBuilder::GetAssetOwnerId() const
     {
        return m_assetOwnerId;
     }

    string AssetRegisterTxBuilder::GetKernelIDString() const {
        Merkle::Hash kernelID;
        m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }

    bool AssetRegisterTxBuilder::GetInputs()
    {
        return m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs, m_SubTxID);
    }

    bool AssetRegisterTxBuilder::GetOutputs()
    {
        return m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs, m_SubTxID);
    }

    void AssetRegisterTxBuilder::SelectInputs()
    {
        /*CoinIDList preselIDs;
        vector<Coin> coins;

        Amount preselAmountBeam  = 0;
        Amount preselAmountAsset = 0;

        auto isAssetCoin = [](const Coin& coin) {
            return coin.m_ID.m_Type == Key::Type::Asset || coin.m_ID.m_Type == Key::Type::AssetChange;
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

        Amount amountBeamWithFee = GetAmountBeam() + m_Fee;
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
            auto selectedCoins = m_Tx.GetWalletDB()->selectCoins(amountAsset - preselAmountAsset, m_assetId);
            if (selectedCoins.empty())
            {
                storage::Totals totals(*m_Tx.GetWalletDB());
                const auto& assetTotals(totals.GetTotals(m_assetId));
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
        */
        CoinIDList preselIDs;
        vector<Coin> coins;

        Amount preselAmountBeam  = 0;
        if (m_Tx.GetParameter(TxParameterID::PreselectedCoins, preselIDs, m_SubTxID))
        {
            if (!preselIDs.empty())
            {
                coins = m_Tx.GetWalletDB()->getCoinsByID(preselIDs);
                for (auto &coin : coins)
                {
                    preselAmountBeam += coin.getAmount();
                    coin.m_spentTxId = m_Tx.GetTxID();
                }
                m_Tx.GetWalletDB()->saveCoins(coins);
            }
        }

        Amount amountBeamWithFee = m_Fee + Rules::get().CA.DepositForList;
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

        m_InputCoins.reserve(coins.size());
        Amount totalBeam = 0;
        for (auto& coin : coins)
        {
            coin.m_spentTxId = m_Tx.GetTxID();
            totalBeam += coin.m_ID.m_Value;
            m_InputCoins.push_back(coin.m_ID);
        }

        m_ChangeBeam  = totalBeam  - amountBeamWithFee;
        m_Tx.SetParameter(TxParameterID::ChangeBeam,  m_ChangeBeam,  false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::InputCoins,  m_InputCoins,  false, m_SubTxID);
        m_Tx.GetWalletDB()->saveCoins(coins);
    }

    void AssetRegisterTxBuilder::GenerateBeamCoin(Amount amount, bool change)
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

    bool AssetRegisterTxBuilder::CreateOutputs()
    {
        if (GetOutputs())
        {
            // if we already have outputs, nothing to do here
            return false;
        }

        if (GetOutputCoins().empty())
        {
            // Output coins should be generated already
            return false;
        }

        DoAsync<IPrivateKeyKeeper::OutputsEx>([this](auto&& r, auto&& ex)
            {
                m_Tx.GetKeyKeeper()->GenerateOutputsEx(m_MinHeight, m_OutputCoins, Zero, move(r), move(ex));
            },
            [this](IPrivateKeyKeeper::OutputsEx&& resOutputs)
            {
                m_Outputs = std::move(resOutputs.first);
                m_Offset += resOutputs.second;
                m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false, m_SubTxID);
            });
        return true; // true if async
    }

    const Merkle::Hash& AssetRegisterTxBuilder::GetKernelID() const
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
                assert(!"KernelID is not stored");
            }
        }
        return *m_KernelID;
    }

    void AssetRegisterTxBuilder::CreateKernel()
    {
        m_regKernel = make_unique<TxKernelAssetCreate>();
        m_regKernel->m_Fee          = m_Fee;
        m_regKernel->m_Height.m_Min = GetMinHeight();
        m_regKernel->m_Height.m_Max = m_MaxHeight;
        m_regKernel->m_Commitment   = Zero;
        m_regKernel->m_Owner        = m_assetOwnerId;
    }

    void AssetRegisterTxBuilder::SignKernel()
    {
        m_Offset += m_keyKeeper->SignAssetKernel(*m_regKernel, m_assetOwnerIdx);

        const Merkle::Hash& kernelID = m_regKernel->m_Internal.m_ID;
        m_Tx.SetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::Kernel, m_regKernel, m_SubTxID);
    }
}

