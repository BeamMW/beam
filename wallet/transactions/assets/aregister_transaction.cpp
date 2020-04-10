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
#include "aissue_tx_builder.h"
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "wallet/core/strings_resources.h"
#include "wallet/core/wallet.h"

namespace beam::wallet
{
    BaseTransaction::Ptr AssetRegisterTransaction::Creator::Create(INegotiatorGateway& gateway, IWalletDB::Ptr walletDB, const TxID& txID)
    {
        return BaseTransaction::Ptr(new AssetRegisterTransaction(gateway, walletDB, txID));
    }

    TxParameters AssetRegisterTransaction::Creator::CheckAndCompleteParameters(const TxParameters& params)
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

    AssetRegisterTransaction::AssetRegisterTransaction(INegotiatorGateway& gateway, IWalletDB::Ptr walletDB, const TxID& txID)
        : AssetTransaction(gateway, std::move(walletDB), txID)
    {
    }

    void AssetRegisterTransaction::UpdateImpl()
    {
        if (!IsLoopbackTransaction())
        {
            OnFailed(TxFailureReason::NotLoopback, true);
            return;
        }

        auto& builder = GetTxBuilder();
        if (!builder.LoadKernel() && GetState() == State::Initial)
        {
            LOG_INFO() << GetTxID() << " Registering asset with the owner ID " << builder.GetAssetOwnerId()
                       << ". Cost is " << PrintableAmount(builder.GetAmountBeam(), false)
                       << ". Fee is "  << PrintableAmount(builder.GetFee(), false);

            if (!builder.GetInitialTxParams())
            {
                builder.SelectInputCoins();
                builder.AddChange();
                UpdateTxDescription(TxStatus::InProgress);
            }

            SetState(State::Making);
            builder.CreateInputs();
            builder.CreateOutputs();
            builder.MakeKernel();
        }

        auto registered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, registered))
        {
            if(const auto ainfo = m_WalletDB->findAsset(builder.GetAssetOwnerId()))
            {
                OnFailed(TxFailureReason::AssetExists);
                return;
            }

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

			SetState(State::Registration);
            m_Gateway.register_tx(GetTxID(), transaction);
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
            ConfirmKernel(builder.GetKernelID());
            return;
        }

        if (GetState() == State::KernelConfirmation)
        {
            LOG_INFO() << GetTxID() << " Asset with the owner ID " << _builder->GetAssetOwnerId() << " successfully registered";
            SetState(State::AssetConfirmation);
            // TODO:ASSETS consider running in separate transaction to not rollback if confirm failed
            ConfirmAsset();
            return;
        }

        if (GetState() == State::AssetConfirmation)
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
            if (!GetParameter(TxParameterID::AssetFullInfo, info))
            {
                OnFailed(TxFailureReason::NoAssetInfo, true);
                return;
            }

            if(_builder->GetAssetOwnerId() != info.m_Owner)
            {
                OnFailed(TxFailureReason::InvalidAssetOwnerId, true);
                return;
            }

            SetParameter(TxParameterID::AssetID, info.m_ID);
        }

        SetState(State::Finalizing);
        std::vector<Coin> modified = m_WalletDB->getCoinsByTx(GetTxID());
        for (auto& coin : modified)
        {
            if (coin.m_createTxId == m_ID)
            {
                std::setmin(coin.m_confirmHeight, kpHeight);
                coin.m_maturity = kpHeight + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
            }
            if (coin.m_spentTxId == m_ID)
            {
                std::setmin(coin.m_spentHeight, kpHeight);
            }
        }

        GetWalletDB()->saveCoins(modified);
        CompleteTx();
    }

    void AssetRegisterTransaction::ConfirmAsset()
    {
        GetGateway().confirm_asset(GetTxID(), _builder->GetAssetOwnerId(), kDefaultSubTxID);
    }

    bool AssetRegisterTransaction::IsLoopbackTransaction() const
    {
        return GetMandatoryParameter<bool>(TxParameterID::IsSender) && IsInitiator();
    }

    bool AssetRegisterTransaction::ShouldNotifyAboutChanges(TxParameterID paramID) const
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

    TxType AssetRegisterTransaction::GetType() const
    {
        return TxType::AssetReg;
    }

    AssetRegisterTransaction::State AssetRegisterTransaction::GetState() const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state);
        return state;
    }

    AssetRegisterTxBuilder& AssetRegisterTransaction::GetTxBuilder()
    {
        if (!_builder)
        {
            _builder = std::make_shared<AssetRegisterTxBuilder>(*this, kDefaultSubTxID);
        }
        return *_builder;
    }

    bool AssetRegisterTransaction::IsInSafety() const
    {
        State txState = GetState();
        return txState >= State::KernelConfirmation;
    }
}
