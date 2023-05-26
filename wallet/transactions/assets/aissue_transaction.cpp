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

#include "aissue_transaction.h"
#include "../../core/base_tx_builder.h"
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "wallet/core/strings_resources.h"
#include "wallet/core/wallet.h"

namespace beam::wallet
{
    AssetIssueTransaction::Creator::Creator(bool issue)
        : _issue(issue)
    {
    }

    BaseTransaction::Ptr AssetIssueTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new AssetIssueTransaction(_issue, context));
    }

    TxParameters AssetIssueTransaction::Creator::CheckAndCompleteParameters(const TxParameters& params)
    {
        TxParameters result{params};
        return result;
    }

    struct AssetIssueTransaction::MyBuilder
        :public AssetTransaction::Builder
    {
        Amount m_Value;

        MyBuilder(AssetIssueTransaction& tx)
            :Builder(tx, kDefaultSubTxID)
        {
            m_Value = m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Amount);
        }

        void Sign(bool bIssue)
        {
            if (m_pKrn)
                return;

            std::unique_ptr<TxKernelAssetEmit> pKrn = std::make_unique<TxKernelAssetEmit>();
            pKrn->m_AssetID = m_Tx.GetMandatoryParameter<Asset::ID>(TxParameterID::AssetID);

            pKrn->m_Value = m_Value;
            if (!bIssue)
                pKrn->m_Value = -pKrn->m_Value;

            AddKernel(std::move(pKrn));
            FinalyzeTx();
        }
    };

    AssetIssueTransaction::AssetIssueTransaction(bool issue, const TxContext& context)
        : AssetTransaction (issue ? TxType::AssetIssue : TxType::AssetConsume, context)
        , _issue(issue)
    {
    }

    void AssetIssueTransaction::UpdateImpl()
    {
        if (!AssetTransaction::BaseUpdate())
        {
            return;
        }

        if (!_builder)
        {
            _builder = std::make_shared<MyBuilder>(*this);
        }

        auto& builder = *_builder;
        if (GetState<State>() == State::Initial)
        {
            LOG_INFO()
                << GetTxID()
                << " "
                << (_issue ? "Generating" : "Consuming")
                << " asset with owner id " << builder.m_pidAsset
                << ". Amount: " << PrintableAmount(builder.m_Value, false, 0, kAmountASSET, kAmountAGROTH);

            UpdateTxDescription(TxStatus::InProgress);
        }

        if (builder.m_Coins.IsEmpty())
        {
            auto pInfo = GetWalletDB()->findAsset(builder.m_pidAsset);
            if (!pInfo)
            {
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
            }

            WalletAsset& wa = *pInfo;
            SetParameter(TxParameterID::AssetID, wa.m_ID);
            SetParameter(TxParameterID::AssetInfoFull, Cast::Down<Asset::Full>(wa));
            SetParameter(TxParameterID::AssetConfirmedHeight, wa.m_RefreshHeight);

            SetState(State::Making);

            BaseTxBuilder::Balance bb(builder);
            bb.m_Map[0].m_Value -= builder.m_Fee;

            if (_issue)
            {
                bb.m_Map[wa.m_ID].m_Value += builder.m_Value;
            }
            else
            {
                bb.m_Map[wa.m_ID].m_Value -= builder.m_Value;
            }

            bb.CompleteBalance();
            builder.SaveCoins();
        }

        builder.GenerateInOuts();
        if (builder.IsGeneratingInOuts())
            return;

        if (!builder.m_pKrn)
            builder.Sign(_issue);

        auto registered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, registered))
        {
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

    bool AssetIssueTransaction::IsInSafety() const
    {
        auto state = GetState<State>();
        return state == State::KernelConfirmation;
    }
}
