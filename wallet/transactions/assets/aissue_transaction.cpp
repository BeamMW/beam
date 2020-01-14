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
#include "aissue_tx_builder.h"
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

    BaseTransaction::Ptr AssetIssueTransaction::Creator::Create(INegotiatorGateway& gateway, IWalletDB::Ptr walletDB, IPrivateKeyKeeper::Ptr keyKeeper, const TxID& txID)
    {
        return BaseTransaction::Ptr(new AssetIssueTransaction(_issue, gateway, walletDB, keyKeeper, txID));
    }

    TxParameters AssetIssueTransaction::Creator::CheckAndCompleteParameters(const TxParameters& params)
    {
        if(params.GetParameter<WalletID>(TxParameterID::PeerID))
        {
            throw InvalidTransactionParametersException();
        }

        if(params.GetParameter<WalletID>(TxParameterID::MyID))
        {
            throw InvalidTransactionParametersException();
        }

        const auto isSenderO = params.GetParameter<bool>(TxParameterID::IsSender);
        if (!isSenderO || !isSenderO.get())
        {
            throw InvalidTransactionParametersException();
        }

        const auto isInitiatorO = params.GetParameter<bool>(TxParameterID::IsInitiator);
        if (!isInitiatorO || !isInitiatorO.get())
        {
            throw InvalidTransactionParametersException();
        }

        TxParameters result{params};
        result.SetParameter(TxParameterID::IsSelfTx, true);
        result.SetParameter(TxParameterID::MyID, WalletID(Zero)); // Mandatory parameter
        return result;
    }

    AssetIssueTransaction::AssetIssueTransaction(bool issue, INegotiatorGateway& gateway
                                        , IWalletDB::Ptr walletDB
                                        , IPrivateKeyKeeper::Ptr keyKeeper
                                        , const TxID& txID)
        : BaseTransaction{ gateway, std::move(walletDB), std::move(keyKeeper), txID}
        , _issue(issue)
    {
    }

    void AssetIssueTransaction::UpdateImpl()
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

        if(!CreateTxBuilder())
        {
            return;
        }

        auto sharedBuilder = m_TxBuilder;
        AssetIssueTxBuilder& builder = *sharedBuilder;

        if (!builder.LoadKernel())
        {
            if (!builder.GetInitialTxParams() && GetState() == State::Initial)
            {
                if (_issue)
                {
                    LOG_INFO() << GetTxID() << " Generating asset with owner index " << builder.GetAssetOwnerIdx()
                               << " and asset id " << builder.GetAssetId().str() << ". Amount: " << PrintableAmount(builder.GetAmountBeam(), false, kASSET, kAGROTH);
                    LOG_INFO() << GetTxID() << " Please remember your assset index. You won't be able to consume the asset or generate additional coins without it";
                }
                else
                {
                    LOG_INFO() << GetTxID() << " Consuming asset with index " << builder.GetAssetOwnerIdx() << " and asset id " << builder.GetAssetId().str()
                               << ". Amount: " << PrintableAmount(builder.GetAmountAsset(), false, kASSET, kAGROTH);
                }

                builder.SelectInputs();
                builder.AddChange();

                for (const auto& amount : builder.GetAmountList())
                {
                    if (_issue) builder.GenerateAssetCoin(amount, false);
                    else builder.GenerateBeamCoin(amount, false);
                }

                UpdateTxDescription(TxStatus::InProgress);
            }

            if (GetState() == State::Initial)
            {
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
                builder.CreateKernel();
                builder.SignKernel();
            }
        }

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
        {
            if (CheckExpired())
            {
                return;
            }

            // Construct & verify transaction
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

        if (proto::TxStatus::Ok != nRegistered)
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
            bool bIn = (coin.m_createTxId == m_ID);
            bool bOut = (coin.m_spentTxId == m_ID);
            if (bIn || bOut)
            {
                if (bIn)
                {
                    coin.m_confirmHeight = std::min(coin.m_confirmHeight, hProof);
                    coin.m_maturity = hProof + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
                }
                if (bOut)
                {
                    coin.m_spentHeight = std::min(coin.m_spentHeight, hProof);
                }
            }
        }

        GetWalletDB()->saveCoins(modified);
        CompleteTx();
    }

    bool AssetIssueTransaction::IsLoopbackTransaction() const
    {
        return GetMandatoryParameter<bool>(TxParameterID::IsSender) && IsInitiator();
    }

    bool AssetIssueTransaction::ShouldNotifyAboutChanges(TxParameterID paramID) const
    {
        switch (paramID)
        {
        case TxParameterID::Amount:
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

    TxType AssetIssueTransaction::GetType() const
    {
        return _issue ? TxType::AssetIssue : TxType::AssetConsume;
    }

    AssetIssueTransaction::State AssetIssueTransaction::GetState() const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state);
        return state;
    }

    bool AssetIssueTransaction::CreateTxBuilder()
    {
        if (!m_TxBuilder)
        {
            AmountList amountList;
            if (!GetParameter(TxParameterID::AmountList, amountList))
            {
                amountList = AmountList{GetMandatoryParameter<Amount>(TxParameterID::Amount)};
            }

            m_TxBuilder = std::make_shared<AssetIssueTxBuilder>(_issue, *this, kDefaultSubTxID, m_KeyKeeper);
        }
        return true;
    }

    bool AssetIssueTransaction::IsInSafety() const
    {
        State txState = GetState();
        return txState == State::KernelConfirmation;
    }
}
