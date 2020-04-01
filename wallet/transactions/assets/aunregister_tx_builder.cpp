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
#include "aunregister_tx_builder.h"
#include "assets_kdf_utils.h"
#include "utility/logger.h"
#include "wallet/core/strings_resources.h"
#include <numeric>
#include <core/block_crypt.h>

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    AssetUnregisterTxBuilder::AssetUnregisterTxBuilder(BaseTransaction& tx, SubTxID subTxID)
        : m_Tx{tx}
        , m_SubTxID(subTxID)
        , m_assetOwnerId(0UL)
        , m_Fee(0)
        , m_MinHeight(0)
        , m_MaxHeight(MaxHeight)
        , m_Offset(Zero)
    {
        auto masterKdf = m_Tx.get_MasterKdfStrict(); // can throw
        m_Fee = m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Fee, m_SubTxID);

        // At the moment we do not require any fee inputs for this transaction
        // and pay fees from ASSET->BEAM conversion. But need to be sure that
        // this conversion generates enough BEAMs
        if (m_Fee > Rules::get().CA.DepositForList)
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoInputs);
        }

        m_Metadata = m_Tx.GetMandatoryParameter<std::string>(TxParameterID::AssetMetadata);
        if(m_Metadata.empty())
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoAssetMeta);
        }

        m_assetOwnerId = GetAssetOwnerID(masterKdf, m_Metadata);
        if (m_assetOwnerId == Zero)
        {
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoAssetId);
        }
    }

    bool AssetUnregisterTxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::Outputs,m_Outputs, m_SubTxID);
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
        return hasOCoins;
    }

    bool AssetUnregisterTxBuilder::LoadKernel()
    {
        if (m_Tx.GetParameter(TxParameterID::Kernel, m_kernel, m_SubTxID))
        {
            GetInitialTxParams();
            return true;
        }
        return false;
    }

    Transaction::Ptr AssetUnregisterTxBuilder::CreateTransaction()
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

    Height AssetUnregisterTxBuilder::GetMinHeight() const
    {
        return m_MinHeight;
    }

    Amount AssetUnregisterTxBuilder::GetFee() const
    {
        return m_Fee;
    }

    PeerID AssetUnregisterTxBuilder::GetAssetOwnerId() const
    {
        return m_assetOwnerId;
    }

    string AssetUnregisterTxBuilder::GetKernelIDString() const {
        Merkle::Hash kernelID;
        m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }

    bool AssetUnregisterTxBuilder::GetOutputs()
    {
        return m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs, m_SubTxID);
    }

    void AssetUnregisterTxBuilder::AddRefund()
    {
        const auto refundAmount = Rules::get().CA.DepositForList - m_Fee;
        if (refundAmount)
        {
            GenerateBeamCoin(refundAmount, false);
        }
    }

    void AssetUnregisterTxBuilder::GenerateBeamCoin(Amount amount, bool change)
    {
        Coin newUtxo(amount, change ? Key::Type::Change : Key::Type::Regular);
        newUtxo.m_createTxId = m_Tx.GetTxID();

        m_Tx.GetWalletDB()->storeCoin(newUtxo);
        m_OutputCoins.push_back(newUtxo.m_ID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);

        LOG_INFO() << m_Tx.GetTxID()
                   << " Creating BEAM coin" << (change ? " (change):" : ":")
                   << PrintableAmount(amount) << ", id " << newUtxo.toStringID();
    }

    void AssetUnregisterTxBuilder::CreateOutputs()
    {
        if (GetOutputs() || m_OutputCoins.empty())
        {
            // if we already have outputs or there are no outputs, nothing to do here
            return;
        }

        auto masterKdf = m_Tx.get_MasterKdfStrict();
        m_Outputs = GenerateAssetOutputs(masterKdf, m_MinHeight, m_OutputCoins);
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false, m_SubTxID);
    }

    const Merkle::Hash& AssetUnregisterTxBuilder::GetKernelID() const
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

    void AssetUnregisterTxBuilder::MakeKernel()
    {
        if (m_kernel) return;

        m_kernel = make_unique<TxKernelAssetDestroy>();
        m_kernel->m_Fee          = m_Fee;
        m_kernel->m_Height.m_Min = GetMinHeight();
        m_kernel->m_Height.m_Max = m_MaxHeight;
        m_kernel->m_Commitment   = Zero;
        m_kernel->m_AssetID      = m_Tx.GetMandatoryParameter<Asset::ID>(TxParameterID::AssetID, m_SubTxID);

        auto masterKdf = m_Tx.get_MasterKdfStrict();
        m_Offset = SignAssetKernel(masterKdf, CoinIDList(), m_OutputCoins, m_Metadata, *m_kernel);
        const Merkle::Hash& kernelID = m_kernel->m_Internal.m_ID;

        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::Kernel, m_kernel, m_SubTxID);
    }
}
