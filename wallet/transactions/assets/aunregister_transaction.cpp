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
        :public AssetTransaction::Builder
    {
        using Builder::Builder;

        void Sign()
        {
            if (m_pKrn)
                return;

            std::unique_ptr<TxKernelAssetDestroy> pKrn = std::make_unique<TxKernelAssetDestroy>();
            pKrn->m_AssetID = m_Tx.GetMandatoryParameter<Asset::ID>(TxParameterID::AssetID);

            AddKernel(std::move(pKrn));
            FinalyzeTx();
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
            _builder = std::make_shared<MyBuilder>(*this, kDefaultSubTxID);
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
            // ALWAYS refresh asset state before destroying
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
                return;
            }

            auto pInfo = GetWalletDB()->findAsset(builder.m_pidAsset);
            if (!pInfo)
                return;

            WalletAsset& wa = *pInfo;
            SetParameter(TxParameterID::AssetID, wa.m_ID);

            if (wa.m_Value != Zero)
            {
                LOG_INFO () << "AID " << wa.m_ID << " value " << AmountBig::get_Lo(wa.m_Value);
                OnFailed(TxFailureReason::AssetInUse);
                return;
            }

            if (wa.CanRollback(builder.m_Height.m_Min))
            {
                OnFailed(TxFailureReason::AssetLocked);
                return;
            }

            SetParameter(TxParameterID::AssetConfirmedHeight, wa.m_RefreshHeight);

            BaseTxBuilder::Balance bb(builder);
            bb.m_Map[0].m_Value += Rules::get().CA.DepositForList - builder.m_Fee;
            bb.CompleteBalance();

            builder.SaveCoins();
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
            OnFailed(TxFailureReason::FailedToRegister);
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

        SetCompletedTxCoinStatuses(kpHeight);
        CompleteTx();
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
