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

#include "aunregister_transaction.h"
#include "../../core/base_tx_builder.h"
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "wallet/core/strings_resources.h"
#include "wallet/core/wallet.h"

namespace beam::wallet
{
    BaseTransaction::Ptr AssetUnregisterTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new AssetUnregisterTransaction(context));
    }

    TxParameters AssetUnregisterTransaction::Creator::CheckAndCompleteParameters(const TxParameters& params)
    {
        if(params.GetParameter<WalletID>(TxParameterID::PeerID))
        {
            throw InvalidTransactionParametersException("Asset registration: unexpected PeerID");
        }

        if(params.GetParameter<WalletID>(TxParameterID::MyID))
        {
            throw InvalidTransactionParametersException("Asset registration: unexpected MyID");
        }

        const auto isSenderO = params.GetParameter<bool>(TxParameterID::IsSender);
        if (!isSenderO || !isSenderO.get())
        {
            throw InvalidTransactionParametersException("Asset registration: non-sender transaction");
        }

        const auto isInitiatorO = params.GetParameter<bool>(TxParameterID::IsInitiator);
        if (!isInitiatorO || !isInitiatorO.get())
        {
            throw InvalidTransactionParametersException("Asset registration: non-initiator transaction");
        }

        TxParameters result{params};
        result.SetParameter(TxParameterID::IsSelfTx, true);
        result.SetParameter(TxParameterID::MyID, WalletID(Zero)); // Mandatory parameter
        return result;
    }

    struct AssetUnregisterTransaction::MyBuilder
        :public BaseTxBuilder
    {
        TxKernelAssetDestroy* m_pKrn = nullptr;
        Asset::Metadata m_Md;

        ECC::Scalar::Native m_skAsset;
        PeerID m_pidAsset;

        void OnKrn(std::unique_ptr<TxKernelAssetDestroy>& pKrn)
        {
            m_pKrn = pKrn.get();
            m_pTransaction->m_vKernels.push_back(std::move(pKrn));
            m_pTransaction->Normalize(); // tx is ready
        }

        MyBuilder(AssetUnregisterTransaction& tx)
            :BaseTxBuilder(tx, kDefaultSubTxID)
        {
            std::unique_ptr<TxKernelAssetDestroy> pKrn;
            m_Tx.GetParameter(TxParameterID::Kernel, pKrn);
            if (pKrn)
                OnKrn(pKrn);

            std::string sMeta = m_Tx.GetMandatoryParameter<std::string>(TxParameterID::AssetMetadata);
            m_Md.m_Value = toByteBuffer(sMeta);
            m_Md.UpdateHash();

            m_Tx.get_MasterKdfStrict()->DeriveKey(m_skAsset, m_Md.m_Hash);
            m_pidAsset.FromSk(m_skAsset);
        }

        void Sign()
        {
            if (m_pKrn)
                return;

            auto pKdf = m_Tx.get_MasterKdfStrict();

            std::unique_ptr<TxKernelAssetDestroy> pKrn = std::make_unique<TxKernelAssetDestroy>();
            pKrn->m_Fee = m_Fee;
            pKrn->m_Height = m_Height;

            pKrn->m_Owner = m_pidAsset;
            pKrn->m_AssetID = m_Tx.GetMandatoryParameter<Asset::ID>(TxParameterID::AssetID);

            ECC::Scalar::Native sk;
            pKrn->get_Sk(sk, *pKdf);
            pKrn->Sign_(sk, m_skAsset);

            m_Tx.SetParameter(TxParameterID::Kernel, pKrn, m_SubTxID);
            OnKrn(pKrn);

            sk = -sk;
            m_Coins.AddOffset(sk, pKdf);

            m_pTransaction->m_Offset = sk;
            m_Tx.SetParameter(TxParameterID::Offset, m_pTransaction->m_Offset, m_SubTxID);

            if (!VerifyTx())
                throw TransactionFailedException(false, TxFailureReason::InvalidTransaction);
        }
    };

    AssetUnregisterTransaction::AssetUnregisterTransaction(const TxContext& context)
        : AssetTransaction(context)
    {
    }

    void AssetUnregisterTransaction::UpdateImpl()
    {
        if (!AssetTransaction::BaseUpdate())
        {
            return;
        }

        if (!_builder)
            _builder = std::make_shared<MyBuilder>(*this);
        auto& builder = *_builder;

        if (GetState() == State::Initial)
        {
            LOG_INFO()
                << GetTxID()
                << " Unregistering asset with the owner id " << builder.m_pidAsset
                << ". Refund amount is " << PrintableAmount(Rules::get().CA.DepositForList, false);

            UpdateTxDescription(TxStatus::InProgress);
        }

        if (builder.m_Coins.IsEmpty())
        {
            // refresh asset state before destroying
            Height h = 0;
            GetParameter(TxParameterID::AssetUnconfirmedHeight, h);
            if (h)
            {
                OnFailed(TxFailureReason::AssetConfirmFailed);
                return;
            }

            h = 0;
            GetParameter(TxParameterID::AssetConfirmedHeight, h);
            if (!h)
            {
                SetState(State::AssetCheck);
                GetGateway().confirm_asset(GetTxID(), _builder->m_pidAsset, kDefaultSubTxID);
            }

            auto pInfo = GetWalletDB()->findAsset(builder.m_pidAsset);
            if (!pInfo)
                return;
            WalletAsset& wa = *pInfo;

            if (wa.m_Value != Zero)
            {
                OnFailed(TxFailureReason::AssetInUse, true);
                return;
            }

            if (wa.CanRollback(builder.m_Height.m_Min))
            {
                OnFailed(TxFailureReason::AssetLocked, true);
                return;
            }

            SetParameter(TxParameterID::AssetConfirmedHeight, wa.m_RefreshHeight);

            if (builder.m_Fee < Rules::get().CA.DepositForList)
            {
                CoinID cid;
                cid.m_Value = Rules::get().CA.DepositForList - builder.m_Fee;
                cid.m_Type = Key::Type::Regular;
                builder.CreateAddNewOutput(cid);
            }
            else
                builder.MakeInputsAndChange(builder.m_Fee - Rules::get().CA.DepositForList, 0);
        }

        builder.GenerateInOuts();
        if (builder.IsGeneratingInOuts())
            return;

        if (!builder.m_pKrn)
            builder.Sign();

        auto registered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, registered))
        {
            SetState(State::Registration);
            GetGateway().register_tx(GetTxID(), builder.m_pTransaction);
            return;
        }

        if (proto::TxStatus::Ok != registered)
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
            return;
        }

        Height kpHeight = 0;
        GetParameter(TxParameterID::KernelProofHeight, kpHeight);
        if (!kpHeight)
        {
            SetState(State::KernelConfirmation);
            ConfirmKernel(builder.m_pKrn->m_Internal.m_ID);
            return;
        }

        CompleteTx();
    }

    bool AssetUnregisterTransaction::ShouldNotifyAboutChanges(TxParameterID paramID) const
    {
        switch (paramID)
        {
        case TxParameterID::Fee:
        case TxParameterID::MinHeight:
        case TxParameterID::CreateTime:
        case TxParameterID::IsSender:
        case TxParameterID::Status:
        case TxParameterID::TransactionType:
        case TxParameterID::KernelID:
            return true;
        default:
            return false;
        }
    }

    TxType AssetUnregisterTransaction::GetType() const
    {
        return TxType::AssetUnreg;
    }

    AssetUnregisterTransaction::State AssetUnregisterTransaction::GetState() const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state);
        return state;
    }

    bool AssetUnregisterTransaction::IsInSafety() const
    {
        State txState = GetState();
        return txState >= State::KernelConfirmation;
    }
}
