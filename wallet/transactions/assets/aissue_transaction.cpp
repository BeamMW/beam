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

    BaseTransaction::Ptr AssetIssueTransaction::Creator::Create(INegotiatorGateway& gateway, IWalletDB::Ptr walletDB, const TxID& txID)
    {
        return BaseTransaction::Ptr(new AssetIssueTransaction(_issue, gateway, walletDB, txID));
    }

    TxParameters AssetIssueTransaction::Creator::CheckAndCompleteParameters(const TxParameters& params)
    {
        if(params.GetParameter<WalletID>(TxParameterID::PeerID))
        {
            throw InvalidTransactionParametersException("PeerID is unexpected for issue/consume");
        }

        if(params.GetParameter<WalletID>(TxParameterID::MyID))
        {
            throw InvalidTransactionParametersException("WalletID is unexpected for issue/consume");
        }

        const auto isSenderO = params.GetParameter<bool>(TxParameterID::IsSender);
        if (!isSenderO || !isSenderO.get())
        {
            throw InvalidTransactionParametersException("Not a sender for issue/consume");
        }

        const auto isInitiatorO = params.GetParameter<bool>(TxParameterID::IsInitiator);
        if (!isInitiatorO || !isInitiatorO.get())
        {
            throw InvalidTransactionParametersException("Not an intiator for issue/consume");
        }

        TxParameters result{params};
        result.SetParameter(TxParameterID::IsSelfTx, true);
        result.SetParameter(TxParameterID::MyID, WalletID(Zero)); // Mandatory parameter
        return result;
    }

    AssetIssueTransaction::AssetIssueTransaction(bool issue, INegotiatorGateway& gateway
                                        , IWalletDB::Ptr walletDB
                                        , const TxID& txID)
        : AssetTransaction{ gateway, std::move(walletDB), txID}
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

        auto& builder = GetTxBuilder();
        if (!builder.LoadKernel() && GetState() == State::Initial)
        {
            if (_issue)
            {
                LOG_INFO() << GetTxID() << " Generating asset with owner id " << builder.GetAssetOwnerId()
                           << ". Amount: " << PrintableAmount(builder.GetTransactionAmount(), false, kASSET, kAGROTH);
            } else
            {
                LOG_INFO() << GetTxID() << " Consuming asset with owner id " << builder.GetAssetOwnerId()
                           << ". Amount: " << PrintableAmount(builder.GetTransactionAmount(), false, kASSET, kAGROTH);
            }

            UpdateTxDescription(TxStatus::InProgress);
            if (const auto info = m_WalletDB->findAsset(builder.GetAssetOwnerId()))
            {
                SetParameter(TxParameterID::AssetID, info->m_ID);
                SetParameter(TxParameterID::AssetFullInfo, *info);
                SetState(State::AssetCheck);
            }
            else
            {
                // TODO:ASSETS consider moving confirmation workflow to base class
                SetState(State::AssetConfirm);
                ConfirmAsset();
                return;
            }
        }

        if (GetState() == State::AssetConfirm)
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

            SetParameter(TxParameterID::AssetID, info.m_ID);
            SetState(State::AssetCheck);
        }

        if (GetState() == State::AssetCheck)
        {
            Asset::Full info;
            if (!GetParameter(TxParameterID::AssetFullInfo, info))
            {
                OnFailed(TxFailureReason::NoAssetInfo, true);
                return;
            }

            if (!info.IsValid())
            {
                OnFailed(TxFailureReason::NoAssetInfo, true);
                return;
            }

            if (info.m_Owner != _builder->GetAssetOwnerId())
            {
                OnFailed(TxFailureReason::InvalidAssetId, true);
                return;
            }

            SetParameter(TxParameterID::AssetID, info.m_ID);
            SetState(State::Making);
        }

        if (GetState() == State::Making)
        {
            if (!builder.GetInitialTxParams())
            {
                builder.SelectInputCoins();
                builder.AddChange();

                if (_issue)
                {
                    for (const auto &amount : builder.GetAmountList())
                    {
                        builder.GenerateAssetCoin(amount, false);
                    }
                }

                builder.CreateInputs();
                builder.CreateOutputs();
                builder.MakeKernel();
            }
        }

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
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

			SetState(State::Registration);
            m_Gateway.register_tx(GetTxID(), transaction);
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

        SetState(State::Finalizing);
        std::vector<Coin> modified = m_WalletDB->getCoinsByTx(GetTxID());
        for (auto& coin : modified)
        {
            bool bIn  = (coin.m_createTxId == m_ID);
            bool bOut = (coin.m_spentTxId == m_ID);
            if (bIn || bOut)
            {
                if (bIn)
                {
                    std::setmin(coin.m_confirmHeight, hProof);
                    coin.m_maturity = hProof + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
                }
                if (bOut)
                {
                    std::setmin(coin.m_spentHeight, hProof);
                }
            }
        }

        GetWalletDB()->saveCoins(modified);
        CompleteTx();
    }

    void AssetIssueTransaction::ConfirmAsset()
    {
        GetGateway().confirm_asset(GetTxID(), _builder->GetAssetOwnerId(), kDefaultSubTxID);
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

    AssetIssueTxBuilder& AssetIssueTransaction::GetTxBuilder()
    {
        if (!_builder)
        {
            _builder = std::make_shared<AssetIssueTxBuilder>(_issue, *this, kDefaultSubTxID);
        }
        return *_builder;
    }

    bool AssetIssueTransaction::IsInSafety() const
    {
        State txState = GetState();
        return txState == State::KernelConfirmation;
    }
}
