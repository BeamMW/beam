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
#include "asset_base_tx.h"

// TODO:ASSETS move what you can to base class
namespace beam::wallet {
    AssetTransaction::AssetTransaction(const TxType txType, const TxContext& context)
        : BaseTransaction(txType, context)
    {
    }

    bool AssetTransaction::Rollback(Height height)
    {
        bool rthis = false;

        Height cheight = 0;
        if (GetParameter(TxParameterID::AssetConfirmedHeight, cheight) && (cheight > height))
        {
            SetParameter(TxParameterID::AssetConfirmedHeight, Height(0));
            SetParameter(TxParameterID::AssetInfoFull, Asset::Full());
            rthis = true;
        }

        Height uheight = 0;
        if (GetParameter(TxParameterID::AssetUnconfirmedHeight, uheight) && (uheight > height))
        {
            SetParameter(TxParameterID::AssetUnconfirmedHeight, Height(0));
            rthis = true;
        }

        const auto rsuper = BaseTransaction::Rollback(height);
        return rthis || rsuper;
    }

    bool AssetTransaction::BaseUpdate()
    {
        Height minHeight = 0;
        if (!GetParameter(TxParameterID::MinHeight, minHeight))
        {
            minHeight = GetWalletDB()->getCurrentHeight();
            SetParameter(TxParameterID::MinHeight, minHeight);
        }

        TxFailureReason enableReason = CheckAssetsEnabled(minHeight);
        if (TxFailureReason::Count != enableReason)
        {
            OnFailed(enableReason);
            return false;
        }

        Height maxHeight = 0;
        if (!GetParameter(TxParameterID::MaxHeight, maxHeight))
        {
            Height lifetime = kDefaultTxLifetime;
            GetParameter(TxParameterID::Lifetime, lifetime);

            maxHeight = minHeight + lifetime;
            SetParameter(TxParameterID::MaxHeight, maxHeight);
        }

        if (!IsLoopbackTransaction())
        {
            OnFailed(TxFailureReason::NotLoopback);
            return false;
        }

        if (CheckExpired())
        {
            return false;
        }

        return true;
    }

    bool AssetTransaction::IsLoopbackTransaction() const
    {
        return GetMandatoryParameter<bool>(TxParameterID::IsSender) && IsInitiator();
    }

    Asset::ID AssetTransaction::GetAssetID() const
    {
        Asset::ID assetId = Asset::s_InvalidID;
        GetParameter(TxParameterID::AssetID, assetId, kDefaultSubTxID);
        return assetId;
    }

    PeerID AssetTransaction::GetAssetOwnerID() const
    {
        std::string strMeta;
        if (GetParameter(TxParameterID::AssetMetadata, strMeta, kDefaultSubTxID) && !strMeta.empty())
        {
            const auto masterKdf = get_MasterKdfStrict(); // can throw
            return beam::wallet::GetAssetOwnerID(masterKdf, strMeta);
        }
        return Asset::s_InvalidOwnerID;
    }

    void AssetTransaction::ConfirmAsset()
    {
        const auto assetId = GetAssetID();
        if (assetId != Asset::s_InvalidID)
        {
            GetGateway().confirm_asset(GetTxID(), assetId, kDefaultSubTxID);
            return;
        }

        const auto aseetOwnerId = GetAssetOwnerID();
        if (aseetOwnerId != Asset::s_InvalidOwnerID)
        {
            GetGateway().confirm_asset(GetTxID(), aseetOwnerId, kDefaultSubTxID);
            return;
        }

        throw TransactionFailedException(true, TxFailureReason::NoAssetMeta);
    }

    AssetTransaction::Builder::Builder(BaseTransaction& tx, SubTxID subTxID)
        :BaseTxBuilder(tx, subTxID)
    {
        std::string sMeta;
        GetParameterStrict(TxParameterID::AssetMetadata, sMeta);

        if (sMeta.empty())
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoAssetMeta);

        m_Md.set_String(sMeta, true);
        m_Md.UpdateHash();

        m_Tx.get_MasterKdfStrict()->DeriveKey(m_skAsset, m_Md.m_Hash);
        m_pidAsset.FromSk(m_skAsset);
    }

    void AssetTransaction::Builder::FinalizeTxInternal()
    {
        assert(m_pKrn);
        auto& krn = Cast::Up<TxKernelAssetControl>(*m_pKrn);

        krn.m_Fee = m_Fee;
        krn.m_Height = m_Height;
        krn.m_Owner = m_pidAsset;

        auto pKdf = m_Tx.get_MasterKdfStrict();

        ECC::Scalar::Native sk;
        krn.get_Sk(sk, *pKdf);
        krn.Sign_(sk, m_skAsset);

        SaveKernel();
        SaveKernelID();

        sk = -sk;
        m_Coins.AddOffset(sk, pKdf);

        m_pTransaction->m_Offset = sk;
        SetParameter(TxParameterID::Offset, m_pTransaction->m_Offset);

        BaseTxBuilder::FinalizeTxInternal();
    }
}