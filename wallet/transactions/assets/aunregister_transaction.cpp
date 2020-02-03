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
#include "aissue_tx_builder.h"
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "wallet/core/strings_resources.h"
#include "wallet/core/wallet.h"

namespace beam::wallet
{
    BaseTransaction::Ptr AssetUnregisterTransaction::Creator::Create(INegotiatorGateway& gateway,
            IWalletDB::Ptr walletDB, const TxID& txID)
    {
        return BaseTransaction::Ptr(new AssetUnregisterTransaction(gateway, walletDB, txID));
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

    AssetUnregisterTransaction::AssetUnregisterTransaction(INegotiatorGateway& gateway
                                        , IWalletDB::Ptr walletDB
                                        , const TxID& txID)
        : BaseTransaction{ gateway, std::move(walletDB), txID}
    {
    }

    void AssetUnregisterTransaction::UpdateImpl()
    {
        if (!IsLoopbackTransaction())
        {
            OnFailed(TxFailureReason::NotLoopback, true);
            return;
        }

        auto& builder = GetTxBuilder();
        if (!builder.LoadKernel())
        {
            if (GetState() == State::Initial)
            {
                LOG_INFO() << GetTxID() << " Unregistering asset with the owner index "
                           << builder.GetAssetOwnerIdx()
                           << ". Refund amount is " << PrintableAmount(Rules::get().CA.DepositForList, false);

                UpdateTxDescription(TxStatus::InProgress);
                SetState(State::AssetCheck);
                ConfirmAsset();
                return;
            }

            if (GetState() == State::AssetCheck)
            {
                Height auHeight = 0;
                GetParameter(TxParameterID::AssetUnconfirmedHeight, auHeight);
                if(auHeight)
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
                if (info.m_ID == 0)
                {
                    OnFailed(TxFailureReason::NoAssetId, true);
                    return;
                }
                SetParameter(TxParameterID::AssetID, info.m_ID);

                // Asset value must be zero
                if (info.m_Value != Zero)
                {
                    OnFailed(TxFailureReason::AssetInUse, true);
                    return;
                }

                // Rollback should be not possible to the height prio the latest use
                Block::SystemState::Full tip;
                GetTip(tip);

                const auto maxRollback = Rules::get().MaxRollback;
                auto rollHeight = tip.m_Height >= maxRollback ? tip.m_Height - maxRollback : 0;
                if (info.m_LockHeight == 0 || info.m_LockHeight > rollHeight)
                {
                    OnFailed(TxFailureReason::AssetLocked, true);
                    return;
                }
            }

            if (GetState() == State::AssetCheck)
            {
                if(!builder.GetInitialTxParams())
                {
                    builder.AddRefund();
                }

                SetState(State::MakingOutputs);
                if (builder.CreateOutputs())
                {
                    return;
                }
            }

            if (GetState() == State::MakingOutputs)
            {
                SetState(State::MakingKernels);
                if(builder.MakeKernel())
                {
                    return;
                }
            }
        }

        auto registered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, registered))
        {
            if (CheckExpired())
            {
                return;
            }

            auto transaction = builder.CreateTransaction();
            TxBase::Context::Params params;
			TxBase::Context ctx(params);
			ctx.m_Height.m_Min = builder.GetMinHeight();

			if (!transaction->IsValid(ctx))
            {
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return;
            }

            m_Gateway.register_tx(GetTxID(), transaction);
            SetState(State::Registration);
            return;
        }

        if (proto::TxStatus::Ok != registered)
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
            return;
        }

        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            SetState(State::KernelConfirmation);
            ConfirmKernel(builder.GetKernelID());
            return;
        }

        SetState(State::Finalizing);
        std::vector<Coin> modified = m_WalletDB->getCoinsByTx(GetTxID());
        for (auto& coin : modified)
        {
            if (coin.m_createTxId == m_ID)
            {
                coin.m_confirmHeight = std::min(coin.m_confirmHeight, hProof);
                coin.m_maturity = hProof + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
            }
            else
            {
                assert(!"Unexpected coins");
            }
        }

        GetWalletDB()->saveCoins(modified);
        CompleteTx();
    }

    void AssetUnregisterTransaction::ConfirmAsset()
    {
        GetGateway().confirm_asset(GetTxID(), _builder->GetAssetOwnerIdx(), _builder->GetAssetOwnerId(), kDefaultSubTxID);
    }

    bool AssetUnregisterTransaction::IsLoopbackTransaction() const
    {
        return GetMandatoryParameter<bool>(TxParameterID::IsSender) && IsInitiator();
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
        case TxParameterID::AssetOwnerIdx:
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

    AssetUnregisterTxBuilder& AssetUnregisterTransaction::GetTxBuilder()
    {
        if (!_builder)
        {
            _builder = std::make_shared<AssetUnregisterTxBuilder>(*this, kDefaultSubTxID);
        }
        return *_builder;
    }

    bool AssetUnregisterTransaction::IsInSafety() const
    {
        State txState = GetState();
        return txState >= State::KernelConfirmation;
    }
}
