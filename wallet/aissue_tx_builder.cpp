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
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "strings_resources.h"
#include <numeric>

namespace beam::wallet
{
    void GenerateRandom(void* p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++)
            ((uint8_t*) p)[i] = (uint8_t) rand();
    }

    void SetRandom(ECC::uintBig& x)
    {
        GenerateRandom(x.m_pData, x.nBytes);
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

    using namespace ECC;
    using namespace std;

    AssetIssueTxBuilder::AssetIssueTxBuilder(bool issue, BaseTransaction& tx, SubTxID subTxID, IPrivateKeyKeeper::Ptr keyKeeper)
        : m_Tx{tx}
        , m_keyKeeper(std::move(keyKeeper))
        , m_SubTxID(subTxID)
        , m_assetId(Zero)
        , m_assetIdx(0)
        , m_issue(issue)
        , m_AmountList{0}
        , m_Fee(0)
        , m_ChangeBeam(0)
        , m_ChangeAsset(0)
        , m_MinHeight(0)
        , m_MaxHeight(MaxHeight)
        , m_Offset(Zero)
    {
        if (!m_keyKeeper.get())
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoKeyKeeper);
        }

        m_Fee = m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Fee, m_SubTxID);
        if (!m_Tx.GetParameter(TxParameterID::AmountList, m_AmountList, m_SubTxID))
        {
            m_AmountList = AmountList{m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Amount, m_SubTxID)};
        }

        m_assetIdx = m_Tx.GetMandatoryParameter<Key::Index>(TxParameterID::AssetIdx);
        m_assetId  = m_keyKeeper->AIDFromKeyIndex(m_assetIdx);
        if (m_assetIdx == 0 && m_assetId == Zero)
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoAssetId);
        }
    }

    bool AssetIssueTxBuilder::CreateInputs()
    {
        if (GetInputs() || GetInputCoins().empty())
        {
            return false;
        }

        auto commitments = m_Tx.GetKeyKeeper()->GeneratePublicKeysSyncEx(m_InputCoins, true, m_assetId, m_Offset);
        m_Inputs.reserve(commitments.size());
        for (const auto& commitment : commitments)
        {
            auto& input = m_Inputs.emplace_back(make_unique<Input>());
            input->m_Commitment = commitment;
        }

        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);
        return false; // true if async operation has run
    }

    bool AssetIssueTxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::ChangeAsset,m_ChangeAsset, m_SubTxID);
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

    bool AssetIssueTxBuilder::LoadKernels()
    {
        if (m_Tx.GetParameter(TxParameterID::Kernel, m_Kernel, m_SubTxID))
        {
            if (m_Tx.GetParameter(TxParameterID::EmissionKernel, m_EmissionKernel, m_SubTxID))
            {
                GetInitialTxParams();
                return true;
            }
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
        tx->m_vKernels.push_back(std::move(m_EmissionKernel));

        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);
        tx->m_Offset = m_Offset;

        tx->Normalize();

        beam::Transaction::Context::Params pars;
        beam::Transaction::Context ctx(pars);
        ctx.m_Height.m_Min = m_MinHeight;
        bool bIsValid = tx->IsValid(ctx);
        //verify_test(bIsValid);
        int a = bIsValid;
        a++;

        return tx;
    }

    Amount AssetIssueTxBuilder::GetAmountBeam() const
    {
        return m_issue ? std::accumulate(m_AmountList.begin(), m_AmountList.end(), 0ULL) : 0;
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
            const auto coin = GenerateBeamChangeCoin(m_ChangeBeam);
            m_OutputCoins.push_back(coin.m_ID);
        }

        if (m_ChangeAsset)
        {
            const auto coin = GenerateAssetChangeCoin(m_ChangeAsset);
            m_OutputCoins.push_back(coin.m_ID);
        }

         m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
    }

    Coin AssetIssueTxBuilder::GenerateBeamChangeCoin(beam::Amount amount) const
    {
        Coin coin(amount);

        coin.m_createTxId = m_Tx.GetTxID();
        coin.m_ID.m_Type  = Key::Type::Change;
        m_Tx.GetWalletDB()->storeCoin(coin);

        return coin;
    }

    Coin AssetIssueTxBuilder::GenerateAssetChangeCoin(beam::Amount amount) const
    {
        Coin coin(amount);

        coin.m_createTxId = m_Tx.GetTxID();
        coin.m_ID.m_Type  = Key::Type::AssetChange;
        m_Tx.GetWalletDB()->storeCoin(coin);

        return coin;
    }

    Amount AssetIssueTxBuilder::GetFee() const
    {
        return m_Fee;
    }

    uint32_t AssetIssueTxBuilder::GetAssetIdx() const
    {
        return m_assetIdx;
    }

     AssetID AssetIssueTxBuilder::GetAssetId() const
     {
        return m_assetId;
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

    void AssetIssueTxBuilder::SelectInputs()
    {
        CoinIDList preselIDs;
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
                auto beamTotals = totals.GetTotals(Zero);
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
                auto assetTotals(totals.GetTotals(m_assetId));
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

    void AssetIssueTxBuilder::GenerateAssetCoin(Amount amount)
    {
        Coin newUtxo(amount, Key::Type::Asset, m_assetId);
        newUtxo.m_createTxId = m_Tx.GetTxID();
        m_Tx.GetWalletDB()->storeCoin(newUtxo);
        m_OutputCoins.push_back(newUtxo.m_ID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
        LOG_INFO() << m_Tx.GetTxID() << " Creating asset coin: "
                   << PrintableAmount(amount, false, kAmountASSET, kAmountAGROTH)
                   << ", id " << newUtxo.toStringID();
    }

    void AssetIssueTxBuilder::GenerateBeamCoin(Amount amount)
    {
        LOG_INFO() << "Creating beam coin " << amount;
        Coin newUtxo(amount);
        newUtxo.m_createTxId = m_Tx.GetTxID();
        m_Tx.GetWalletDB()->storeCoin(newUtxo);
        m_OutputCoins.push_back(newUtxo.m_ID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
    }

    bool AssetIssueTxBuilder::CreateOutputs()
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

        auto thisHolder = shared_from_this();
        auto txHolder = m_Tx.shared_from_this(); // increment use counter of tx object. We use it to avoid tx object desctruction during Update call.
        m_Tx.GetAsyncAcontext().OnAsyncStarted();

        m_Tx.GetKeyKeeper()->GenerateOutputsEx(m_MinHeight, m_OutputCoins, m_assetId, m_Offset,
            [thisHolder, this, txHolder](auto&& result)
            {
                m_Outputs = move(result);
                m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false, m_SubTxID);
                m_Tx.Update(); // may complete tranasction
                m_Tx.GetAsyncAcontext().OnAsyncFinished();
            },
            [thisHolder, this, txHolder](const exception&)
            {
                m_Tx.GetAsyncAcontext().OnAsyncFinished();
            });

        return true; // true if async
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

    const Merkle::Hash& AssetIssueTxBuilder::GetEmissionKernelID() const
    {
        if (!m_EmissionKernelID)
        {
            Merkle::Hash kernelID;
            if (m_Tx.GetParameter(TxParameterID::EmissionKernelID, kernelID, m_SubTxID))
            {
                m_EmissionKernelID = kernelID;
            }
            else
            {
                assert(false && "EmissionKernelID is not stored");
            }

        }
        return *m_EmissionKernelID;
    }

    void AssetIssueTxBuilder::CreateKernels()
    {
        static_assert(std::is_same<decltype(m_EmissionKernel->m_AssetEmission), int64_t>::value,
                      "If this fails please update value in the ConsumeAmountTooBig's message and typecheck in this assert");

        assert(!m_Kernel);
        if (!m_issue)
        {
            // Note, m_EmissionKernel is still nullptr, do not don't anything except typecheks here
            // This should never happen, nobody would ever have so much asset/beams, but just in case
            if (GetAmountAsset() > (Amount)std::numeric_limits<decltype(m_EmissionKernel->m_AssetEmission)>::max())
            {
                throw TransactionFailedException(!m_Tx.IsInitiator(), ConsumeAmountTooBig);
            }
        }

        m_Kernel = make_unique<TxKernel>();
        m_Kernel->m_Fee          = m_Fee;
        m_Kernel->m_Height.m_Min = GetMinHeight();
        m_Kernel->m_Height.m_Max = m_MaxHeight;
        m_Kernel->m_Commitment   = Zero;

        m_EmissionKernel = make_unique<TxKernel>();
        m_EmissionKernel->m_AssetEmission  = m_issue ? GetAmountBeam() : -static_cast<AmountSigned>(GetAmountAsset());
        m_EmissionKernel->m_Height.m_Min   = GetMinHeight();
        m_EmissionKernel->m_Height.m_Max   = m_MaxHeight;
        m_EmissionKernel->m_Commitment     = Zero;
    }

    void AssetIssueTxBuilder::SignKernels()
    {
        //
        // Kernel
        //
        m_keyKeeper->SignEmissionInOutKernel(m_Kernel, m_assetIdx, m_Offset);

        Merkle::Hash kernelID;
        m_Kernel->get_ID(kernelID);
        m_Tx.SetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::Kernel, m_Kernel, m_SubTxID);

        //
        // Emission kernel
        //
        m_keyKeeper->SignEmissionKernel(m_EmissionKernel, m_assetIdx, m_Offset);

        Merkle::Hash emissionKernelID;
        m_Kernel->get_ID(emissionKernelID);
        m_Tx.SetParameter(TxParameterID::EmissionKernelID, emissionKernelID, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::EmissionKernel, m_EmissionKernel, m_SubTxID);
    }
}

