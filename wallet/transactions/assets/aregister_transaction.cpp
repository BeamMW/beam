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

#include "aregister_transaction.h"
#include "../../core/base_tx_builder.h"
#include "core/block_crypt.h"
#include "wallet/core/strings_resources.h"
#include "wallet/core/wallet.h"
#include "utility/logger.h"

namespace beam::wallet
{
    BaseTransaction::Ptr AssetRegisterTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new AssetRegisterTransaction(context));
    }

    TxParameters AssetRegisterTransaction::Creator::CheckAndCompleteParameters(const TxParameters& params)
    {
        TxParameters result{params};
        return result;
    }

    AssetRegisterTransaction::AssetRegisterTransaction(const TxContext& context)
        : AssetTransaction(TxType::AssetReg, context)
    {
    }

    struct AssetRegisterTransaction::MyBuilder
        :public AssetTransaction::Builder
    {
        MyBuilder(AssetRegisterTransaction& tx)
            :Builder(tx, kDefaultSubTxID)
        {
        }

        void Sign()
        {
            if (m_pKrn)
                return;

            std::unique_ptr<TxKernelAssetCreate> pKrn = std::make_unique<TxKernelAssetCreate>();
            pKrn->m_MetaData = m_Md;
            AddKernel(std::move(pKrn));

            FinalyzeTx();
        }
    };

    void AssetRegisterTransaction::UpdateImpl()
    {
        if (!AssetTransaction::BaseUpdate())
            return;

        if (!_builder)
            _builder = std::make_shared<MyBuilder>(*this);

        auto& builder = *_builder;

        Amount valDeposit = Rules::get().get_DepositForCA(builder.m_Height.m_Min);

        if (builder.m_Coins.IsEmpty())
        {
            BaseTxBuilder::Balance bb(builder);
            bb.m_Map[0].m_Value -= (valDeposit + builder.m_Fee);
            bb.CompleteBalance();

            builder.SaveCoins();

            UpdateTxDescription(TxStatus::InProgress);
            SetState(State::Making);
        }

        builder.GenerateInOuts();
        if (builder.IsGeneratingInOuts())
            return; // Sign() would verify the tx, but it can't be verified until all in/outs are prepared

        if (!builder.m_pKrn)
        {
            builder.Sign();

            BEAM_LOG_INFO() << GetTxID() << " Registering asset with the owner ID " << builder.m_pKrn->CastTo_AssetCreate().m_Owner
                << ". Cost is " << PrintableAmount(valDeposit, false)
                << ". Fee is " << PrintableAmount(builder.m_Fee, false);
        }

        auto registered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, registered))
        {
            if(const auto ainfo = GetWalletDB()->findAsset(builder.m_pKrn->CastTo_AssetCreate().m_Owner))
            {
                OnFailed(TxFailureReason::AssetExists);
                return;
            }

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

        if (GetState<State>() == State::KernelConfirmation)
        {
            BEAM_LOG_INFO() << GetTxID() << " Asset with the owner ID " << builder.m_pKrn->CastTo_AssetCreate().m_Owner << " successfully registered";
            SetState(State::AssetConfirmation);
            ConfirmAsset();
            return;
        }

        if (GetState<State>() == State::AssetConfirmation)
        {
            Height auHeight = 0;
            if(GetParameter(TxParameterID::AssetUnconfirmedHeight, auHeight) && auHeight != 0)
            {
                OnFailed(TxFailureReason::AssetConfirmFailed);
                return;
            }

            Height acHeight = 0;
            if(!GetParameter(TxParameterID::AssetConfirmedHeight, acHeight) || acHeight == 0)
            {
                ConfirmAsset();
                return;
            }

            SetState(State::AssetCheck);
        }

        if(GetState<State>() == State::AssetCheck)
        {
            Asset::Full info;
            if (!GetParameter(TxParameterID::AssetInfoFull, info) || !info.IsValid())
            {
                OnFailed(TxFailureReason::NoAssetInfo);
                return;
            }

            if(GetAssetOwnerID() != info.m_Owner)
            {
                OnFailed(TxFailureReason::InvalidAssetOwnerId);
                return;
            }

            SetParameter(TxParameterID::AssetID, info.m_ID);
        }

        SetState(State::Finalizing);
        SetCompletedTxCoinStatuses(kpHeight);
        CompleteTx();
    }

    bool AssetRegisterTransaction::IsInSafety() const
    {
        auto state = GetState<State>();
        return state >= State::KernelConfirmation;
    }
}
