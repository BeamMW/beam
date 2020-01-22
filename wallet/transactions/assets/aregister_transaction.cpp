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
    BaseTransaction::Ptr AssetRegisterTransaction::Creator::Create(INegotiatorGateway& gateway, IWalletDB::Ptr walletDB, IPrivateKeyKeeper::Ptr keyKeeper, const TxID& txID)
    {
        return BaseTransaction::Ptr(new AssetRegisterTransaction(gateway, walletDB, keyKeeper, txID));
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

    AssetRegisterTransaction::AssetRegisterTransaction(INegotiatorGateway& gateway
                                        , IWalletDB::Ptr walletDB
                                        , IPrivateKeyKeeper::Ptr keyKeeper
                                        , const TxID& txID)
        : BaseTransaction{ gateway, std::move(walletDB), std::move(keyKeeper), txID}
    {
    }

    void AssetRegisterTransaction::UpdateImpl()
    {
        if (!IsLoopbackTransaction())
        {
            OnFailed(TxFailureReason::NotLoopback, true);
            return;
        }

        if (!m_KeyKeeper)
        {
            OnFailed(TxFailureReason::NoKeyKeeper, true);
            return;
        }

        auto& builder = GetTxBuilder();
        if (!builder.LoadKernel())
        {
            if (GetState() == State::Initial)
            {
                if (!builder.GetInitialTxParams())
                {
                    LOG_INFO() << GetTxID() << " Registering asset with the owner index " << builder.GetAssetOwnerIdx() << ". Cost is " << PrintableAmount(builder.GetAmountBeam(), false, kASSET, kAGROTH);
                    LOG_INFO() << GetTxID() << " Please remember your owner assset index. You won't be able to unregister the asset, consume it  or generate additional coins without the owner index";

                    builder.SelectInputs();
                    builder.AddChange();

                    UpdateTxDescription(TxStatus::InProgress);
                }

                SetState(State::MakingInputs);
                if (builder.CreateInputs())
                {
                    return;
                }
            }

            if(GetState() == State::MakingInputs)
            {
                SetState(State::MakingOutputs);
                if (builder.CreateOutputs())
                {
                    return;
                }
            }

            if(GetState() == State::MakingOutputs)
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

        std::vector<Coin> modified = m_WalletDB->getCoinsByTx(GetTxID());
        for (auto& coin : modified)
        {
            if (coin.m_createTxId == m_ID)
            {
                coin.m_confirmHeight = std::min(coin.m_confirmHeight, hProof);
                coin.m_maturity = hProof + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
            }
            if (coin.m_spentTxId == m_ID)
            {
                coin.m_spentHeight = std::min(coin.m_spentHeight, hProof);
            }
        }

        GetWalletDB()->saveCoins(modified);
        CompleteTx();
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
        case TxParameterID::AssetOwnerIdx:
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
        return txState == State::KernelConfirmation;
    }
}
