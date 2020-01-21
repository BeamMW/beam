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
#include <core/block_crypt.h>

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    AssetRegisterTxBuilder::AssetRegisterTxBuilder(BaseTransaction& tx, SubTxID subTxID)
        : m_Tx{tx}
        , m_SubTxID(subTxID)
        , m_assetOwnerIdx(0)
        , m_Fee(0)
        , m_ChangeBeam(0)
        , m_AmountList{0}
        , m_MinHeight(0)
        , m_MaxHeight(MaxHeight)
        , m_Offset(Zero)
    {
        if (!m_Tx.GetKeyKeeper())
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoKeyKeeper);
        }

        if (!m_Tx.GetParameter(TxParameterID::AmountList, m_AmountList, m_SubTxID))
        {
            const auto amount = m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Amount, m_SubTxID);
            if (amount < Rules::get().CA.DepositForList)
            {
                throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::RegisterAmountTooSmall);
            }
            m_AmountList = AmountList{amount};
            m_Tx.SetParameter(TxParameterID::AmountList, m_AmountList, m_SubTxID);
        }

        m_Fee = m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Fee, m_SubTxID);
        m_assetOwnerIdx = m_Tx.GetMandatoryParameter<Key::Index>(TxParameterID::AssetOwnerIdx);

        if (m_assetOwnerIdx == 0)
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

        DoAsync<IPrivateKeyKeeper::PublicKeys>([this](auto&& res, auto&& ex)
            {
                m_Tx.GetKeyKeeper()->GeneratePublicKeys(m_InputCoins, true, move(res), move(ex));
            },
            [this](IPrivateKeyKeeper::PublicKeys&& commitments)
            {
                m_Inputs.reserve(commitments.size());
                for (const auto& commitment : commitments)
                {
                    auto& input = m_Inputs.emplace_back(make_unique<Input>());
                    input->m_Commitment = commitment;
                }
               m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);
            });

        return true;
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
        if (m_Tx.GetParameter(TxParameterID::Kernel, m_kernel, m_SubTxID))
        {
            GetInitialTxParams();
            return true;
        }
        return false;
    }

    Transaction::Ptr AssetRegisterTxBuilder::CreateTransaction()
    {
        // Don't display in log infinite max height
        if (m_kernel->m_Height.m_Max == MaxHeight)
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << m_kernel->m_Height.m_Min;
        }
        else
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << m_kernel->m_Height.m_Min
                << ", max height: " << m_kernel->m_Height.m_Max;
        }

        auto tx = make_shared<Transaction>();

        tx->m_vInputs  = std::move(m_Inputs);
        tx->m_vOutputs = std::move(m_Outputs);
        tx->m_vKernels.push_back(std::move(m_kernel));

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

    Amount AssetRegisterTxBuilder::GetAmountBeam() const
    {
        return std::accumulate(m_AmountList.begin(), m_AmountList.end(), 0ULL);
    }

    const AmountList& AssetRegisterTxBuilder::GetAmountList() const
    {
        return m_AmountList;
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
        CoinIDList preselIDs;
        vector<Coin> coins;

        Amount preselAmount  = 0;
        if (m_Tx.GetParameter(TxParameterID::PreselectedCoins, preselIDs, m_SubTxID))
        {
            if (!preselIDs.empty())
            {
                coins = m_Tx.GetWalletDB()->getCoinsByID(preselIDs);
                for (auto &coin : coins)
                {
                    preselAmount += coin.getAmount();
                    coin.m_spentTxId = m_Tx.GetTxID();
                }
                m_Tx.GetWalletDB()->saveCoins(coins);
            }
        }

        Amount amountWithFee = m_Fee + GetAmountBeam();
        if (preselAmount < amountWithFee)
        {
            auto selectedCoins = m_Tx.GetWalletDB()->selectCoins(amountWithFee - preselAmount, Zero);
            if (selectedCoins.empty())
            {
                storage::Totals totals(*m_Tx.GetWalletDB());
                const auto& beamTotals = totals.GetTotals(Zero);
                LOG_ERROR() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                            << "Yout need " << PrintableAmount(amountWithFee) << " for deposit and fees"
                            << " but have only " << PrintableAmount(beamTotals.Avail);
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

        m_ChangeBeam  = totalBeam  - amountWithFee;
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
            assert(!"Otput coins should be generated already");
            return false;
        }

        DoAsync<IPrivateKeyKeeper::Outputs>([this](auto&& res, auto&& ex)
            {
                m_Tx.GetKeyKeeper()->GenerateOutputs(m_MinHeight, m_OutputCoins, move(res), move(ex));
            },
            [this](IPrivateKeyKeeper::Outputs&& outputs)
            {
                m_Outputs = std::move(outputs);
                m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false, m_SubTxID);
            });

        return true; // true if async
    }

    const Merkle::Hash& AssetRegisterTxBuilder::GetKernelID() const
    {
        if (!m_kernelID)
        {
            Merkle::Hash kernelID;
            if (m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID))
            {
                m_kernelID = kernelID;
            }
            else
            {
                assert(!"KernelID is not stored");
            }
        }
        return *m_kernelID;
    }

    bool AssetRegisterTxBuilder::MakeKernel()
    {
        if (!m_kernel)
        {
            m_kernel = make_unique<TxKernelAssetCreate>();
            m_kernel->m_Fee = m_Fee;
            m_kernel->m_Height.m_Min = GetMinHeight();
            m_kernel->m_Height.m_Max = m_MaxHeight;
            m_kernel->m_Commitment = Zero;

            DoAsync<ECC::Scalar::Native>([this](auto&& res, auto&& ex)
            {
                m_Tx.GetKeyKeeper()->SignAssetKernel(m_InputCoins, m_OutputCoins, m_Fee, m_assetOwnerIdx, *m_kernel, move(res), move(ex));
            },
            [this](ECC::Scalar::Native&& offset)
            {
                m_Offset = offset;
                m_Tx.SetParameter(TxParameterID::Offset, m_Offset, m_SubTxID);

                const Merkle::Hash& kernelID = m_kernel->m_Internal.m_ID;
                m_Tx.SetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
                m_Tx.SetParameter(TxParameterID::Kernel, m_kernel, m_SubTxID);
            });
            return true;
        }
        return false;
    }
}
