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

#include "ainfo_transaction.h"
#include "utility/logger.h"
#include "wallet/core/strings_resources.h"
#include "wallet/core/wallet.h"

namespace beam::wallet
{
    BaseTransaction::Ptr AssetInfoTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new AssetInfoTransaction(context));
    }

    TxParameters AssetInfoTransaction::Creator::CheckAndCompleteParameters(const TxParameters& params)
    {
        TxParameters result{params};
        result.SetParameter(TxParameterID::Amount, Amount(0)); // Mandatory parameter
        return result;
    }

    AssetInfoTransaction::AssetInfoTransaction(const TxContext& context)
        : AssetTransaction(TxType::AssetInfo, context)
    {
    }

    void AssetInfoTransaction::UpdateImpl()
    {
        if (!AssetTransaction::BaseUpdate())
        {
            return;
        }

        if (GetState<State>() == State::Initial)
        {
            UpdateTxDescription(TxStatus::InProgress);
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

        if (GetState<State>() == State::AssetCheck)
        {
            Asset::Full info;
            if (!GetParameter(TxParameterID::AssetInfoFull, info) || !info.IsValid())
            {
                OnFailed(TxFailureReason::NoAssetInfo);
                return;
            }

            if (GetAssetID() != Asset::s_InvalidID)
            {
                if (GetAssetID() != info.m_ID)
                {
                    OnFailed(TxFailureReason::InvalidAssetId);
                    return;
                }
            }

            if (GetAssetOwnerID() != Asset::s_InvalidOwnerID)
            {
                if(GetAssetOwnerID() != info.m_Owner)
                {
                    OnFailed(TxFailureReason::InvalidAssetOwnerId);
                    return;
                }
            }

            std::string strMeta;
            info.m_Metadata.get_String(strMeta);
            SetParameter(TxParameterID::AssetMetadata, strMeta);
            SetParameter(TxParameterID::AssetID, info.m_ID);
        }

        SetState(State::Finalzing);
        CompleteTx();
    }

    bool AssetInfoTransaction::IsInSafety() const
    {
        auto state = GetState<State>();
        return state >= State::AssetCheck;
    }
}
