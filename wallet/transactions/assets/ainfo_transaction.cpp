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
    BaseTransaction::Ptr AssetInfoTransaction::Creator::Create(INegotiatorGateway& gateway,
            IWalletDB::Ptr walletDB, const TxID& txID)
    {
        return BaseTransaction::Ptr(new AssetInfoTransaction(gateway, walletDB, txID));
    }

    TxParameters AssetInfoTransaction::Creator::CheckAndCompleteParameters(const TxParameters& params)
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
        result.SetParameter(TxParameterID::Amount, Amount(0)); // Mandatory parameter
        return result;
    }

    AssetInfoTransaction::AssetInfoTransaction(INegotiatorGateway& gateway
                                        , IWalletDB::Ptr walletDB
                                        , const TxID& txID)
        : BaseTransaction{ gateway, std::move(walletDB), txID}
    {
    }

    void AssetInfoTransaction::UpdateImpl()
    {
        if (!IsLoopbackTransaction())
        {
            OnFailed(TxFailureReason::NotLoopback, true);
            return;
        }

        if (GetState() == State::Initial)
        {
            if (GetAssetID() == Asset::s_InvalidID)
            {
                OnFailed(TxFailureReason::NoAssetId, true);
                return;
            }

            UpdateTxDescription(TxStatus::InProgress);
            SetState(State::AssetCheck);
            ConfirmAsset();
            return;
        }

        if (GetState() == State::AssetCheck)
        {
            Height auHeight = 0;
            GetParameter(TxParameterID::AssetUnconfirmedHeight, auHeight);
            if (auHeight)
            {
                OnFailed(TxFailureReason::AssetConfirmFailed);
                return;
            }

            Height acHeight = 0;
            GetParameter(TxParameterID::AssetConfirmedHeight, acHeight);
            if (!acHeight)
            {
                ConfirmAsset();
                return;
            }

            Asset::Full info;
            if (!GetParameter(TxParameterID::AssetFullInfo, info) || !info.IsValid())
            {
                OnFailed(TxFailureReason::NoAssetInfo, true);
                return;
            }

            // Asset ID must be valid
            if (info.m_ID != GetAssetID())
            {
                OnFailed(TxFailureReason::InvalidAssetId, true);
                return;
            }
        }

        CompleteTx();
    }

    void AssetInfoTransaction::ConfirmAsset()
    {
        GetGateway().confirm_asset(GetTxID(), GetAssetID(), kDefaultSubTxID);
    }

    bool AssetInfoTransaction::IsLoopbackTransaction() const
    {
        return GetMandatoryParameter<bool>(TxParameterID::IsSender) && IsInitiator();
    }

    bool AssetInfoTransaction::ShouldNotifyAboutChanges(TxParameterID paramID) const
    {
        switch (paramID)
        {
        case TxParameterID::MinHeight:
        case TxParameterID::CreateTime:
        case TxParameterID::IsSender:
        case TxParameterID::Status:
        case TxParameterID::TransactionType:
            return true;
        default:
            return false;
        }
    }

    TxType AssetInfoTransaction::GetType() const
    {
        return TxType::AssetInfo;
    }

    AssetInfoTransaction::State AssetInfoTransaction::GetState() const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state);
        return state;
    }

    bool AssetInfoTransaction::IsInSafety() const
    {
        State txState = GetState();
        return txState >= State::AssetCheck;
    }

    Asset::ID AssetInfoTransaction::GetAssetID() const
    {
        Asset::ID assetId = Asset::s_InvalidID;
        GetParameter(TxParameterID::AssetID, assetId, kDefaultSubTxID);
        return assetId;
    }
}
