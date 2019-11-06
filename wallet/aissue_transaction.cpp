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

    AssetIssueTransaction::AssetIssueTransaction(bool issue, INegotiatorGateway& gateway
                                        , IWalletDB::Ptr walletDB
                                        , IPrivateKeyKeeper::Ptr keyKeeper
                                        , const TxID& txID)
        : BaseTransaction{ gateway, walletDB, keyKeeper, txID }
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

        if (!builder.LoadKernels())
        {
            if (!builder.GetInitialTxParams() && GetState() == State::Initial)
            {
                if (_issue) {
                    LOG_INFO() << GetTxID() << " Issuing asset  " << PrintableAmount(builder.GetAmountBeam()) << " (fee: "
                               << PrintableAmount(builder.GetFee()) << ")";
                } else {
                    LOG_INFO()  << GetTxID() << " Consuming asset  " << PrintableAmount(builder.GetAmountAsset()) << " (fee: " << PrintableAmount(builder.GetFee()) << ")";
                }

                builder.SelectInputs();
                builder.AddChange();

                for (const auto& amount : builder.GetAmountList())
                {
                    if (_issue) builder.GenerateAssetCoin(amount);
                    else builder.GenerateBeamCoin(amount);
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
                builder.CreateKernels();
                builder.SignKernels(m_KeyKeeper);
            }
        }

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
        {
            if (CheckExpired())
            {
                return;
            }

            // Construct transaction
            auto transaction = builder.CreateTransaction();

            // Verify final transaction
            TxBase::Context::Params pars;
			TxBase::Context ctx(pars);
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
        case TxParameterID::PeerID:
        case TxParameterID::MyID:
        case TxParameterID::CreateTime:
        case TxParameterID::IsSender:
        case TxParameterID::Status:
        case TxParameterID::TransactionType:
        case TxParameterID::KernelID:
        case TxParameterID::EmissionKernelID:
        case TxParameterID::AssetIdx:
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

            uint64_t ownID;
            if (!GetParameter(TxParameterID::MyAddressID, ownID))
            {
                WalletID walletId;
                if (GetParameter(TxParameterID::MyID, walletId))
                {
                    auto waddr = m_WalletDB->getAddress(walletId);
                    if(!(waddr && waddr->m_OwnID))
                    {
                        OnFailed(TxFailureReason::NotLoopback, true);
                        return false;
                    }

                    ownID = waddr->m_OwnID;
                    SetParameter(TxParameterID::MyAddressID, ownID);
                }
            }

            auto assetIdx   = GetMandatoryParameter<Key::Index>(TxParameterID::AssetIdx);
            auto assetKeyId = Key::ID(assetIdx, Key::Type::Asset, assetIdx);
            auto assetId    = m_KeyKeeper->AIDFromKeyID(assetKeyId);

            m_TxBuilder = std::make_shared<AssetIssueTxBuilder>(_issue, *this, kDefaultSubTxID, amountList,
                    GetMandatoryParameter<Amount>(TxParameterID::Fee), assetKeyId, assetId
            );
        }
        return true;
    }
}
