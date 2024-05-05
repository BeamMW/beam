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
        TxParameters result{params};
        return result;
    }

    struct AssetUnregisterTransaction::MyBuilder
        :public AssetTransaction::Builder
    {
        using Builder::Builder;

        void Sign(Amount valDeposit)
        {
            if (m_pKrn)
                return;

            std::unique_ptr<TxKernelAssetDestroy> pKrn = std::make_unique<TxKernelAssetDestroy>();
            pKrn->m_AssetID = m_Tx.GetMandatoryParameter<Asset::ID>(TxParameterID::AssetID);

            if (m_Height.m_Min >= Rules::get().pForks[5].m_Height)
                pKrn->m_Deposit = valDeposit;

            AddKernel(std::move(pKrn));
            FinalyzeTx();
        }
    };

    AssetUnregisterTransaction::AssetUnregisterTransaction(const TxContext& context)
        : AssetTransaction(TxType::AssetUnreg, context)
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

        auto pInfo = GetWalletDB()->findAsset(builder.m_pidAsset);
        if (!pInfo)
        {
            OnFailed(TxFailureReason::NoAssetInfo);
            return;
        }

        WalletAsset& wa = *pInfo;

        if (GetState<State>() == State::Initial)
        {
            BEAM_LOG_INFO()
                << GetTxID()
                << " Unregistering asset with the owner id " << builder.m_pidAsset
                << ". Refund amount is " << PrintableAmount(wa.m_Deposit, false);

            UpdateTxDescription(TxStatus::InProgress);
        }

        if (builder.m_Coins.IsEmpty())
        {
            //
            // ALWAYS refresh asset state before destroying
            //
            Height ucHeight = 0;
            if(GetParameter(TxParameterID::AssetUnconfirmedHeight, ucHeight) && ucHeight != 0)
            {
                OnFailed(TxFailureReason::AssetConfirmFailed);
                return;
            }

            Height acHeight = 0;
            if(!GetParameter(TxParameterID::AssetConfirmedHeight, acHeight) || acHeight == 0)
            {
                SetState(State::AssetConfirmation);
                ConfirmAsset();
                return;
            }

            SetParameter(TxParameterID::AssetID, wa.m_ID);

            if (wa.m_Value != Zero)
            {
                BEAM_LOG_INFO () << "AID " << wa.m_ID << " value " << AmountBig::get_Lo(wa.m_Value);
                OnFailed(TxFailureReason::AssetInUse);
                return;
            }

            if (wa.CanRollback(builder.m_Height.m_Min))
            {
                OnFailed(TxFailureReason::AssetLocked);
                return;
            }

            BaseTxBuilder::Balance bb(builder);
            bb.m_Map[0].m_Value += wa.m_Deposit - builder.m_Fee;
            bb.CompleteBalance();

            builder.SaveCoins();
        }

        builder.GenerateInOuts();
        if (builder.IsGeneratingInOuts())
            return;

        if (!builder.m_pKrn)
            builder.Sign(pInfo->m_Deposit);

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

    bool AssetUnregisterTransaction::IsInSafety() const
    {
        const auto state = GetState<State>();
        return state >= State::KernelConfirmation;
    }
}
