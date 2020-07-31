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

    struct AssetIssueTransaction::MyBuilder
        :public BaseTxBuilder
    {
        TxKernelAssetEmit* m_pKrn = nullptr;
        Asset::Metadata m_Md;

        Amount m_Value;
        ECC::Scalar::Native m_skAsset;
        PeerID m_pidAsset;

        void OnKrn(std::unique_ptr<TxKernelAssetEmit>& pKrn)
        {
            m_pKrn = pKrn.get();
            m_pTransaction->m_vKernels.push_back(std::move(pKrn));
            m_pTransaction->Normalize(); // tx is ready
        }

        MyBuilder(AssetIssueTransaction& tx)
            :BaseTxBuilder(tx, kDefaultSubTxID)
        {
            std::unique_ptr<TxKernelAssetEmit> pKrn;
            m_Tx.GetParameter(TxParameterID::Kernel, pKrn);
            if (pKrn)
                OnKrn(pKrn);

            m_Value = m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Amount);

            std::string sMeta = m_Tx.GetMandatoryParameter<std::string>(TxParameterID::AssetMetadata);
            m_Md.m_Value = toByteBuffer(sMeta);
            m_Md.UpdateHash();

            m_Tx.get_MasterKdfStrict()->DeriveKey(m_skAsset, m_Md.m_Hash);
            m_pidAsset.FromSk(m_skAsset);
        }

        void Sign(bool bIssue)
        {
            if (m_pKrn)
                return;

            auto pKdf = m_Tx.get_MasterKdfStrict();

            std::unique_ptr<TxKernelAssetEmit> pKrn = std::make_unique<TxKernelAssetEmit>();
            pKrn->m_Fee = m_Fee;
            pKrn->m_Height = m_Height;

            pKrn->m_Owner = m_pidAsset;
            pKrn->m_AssetID = m_Tx.GetMandatoryParameter<Asset::ID>(TxParameterID::AssetID);

            pKrn->m_Value = m_Value;
            if (!bIssue)
                pKrn->m_Value = -pKrn->m_Value;

            ECC::Scalar::Native sk;
            pKrn->get_Sk(sk, *pKdf);
            pKrn->Sign_(sk, m_skAsset);

            m_Tx.SetParameter(TxParameterID::Kernel, pKrn, m_SubTxID);
            OnKrn(pKrn);

            sk = -sk;
            m_Coins.AddOffset(sk, pKdf);

            m_pTransaction->m_Offset = sk;
            m_Tx.SetParameter(TxParameterID::Offset, m_pTransaction->m_Offset, m_SubTxID);

            VerifyTx();
        }
    };

    AssetIssueTransaction::AssetIssueTransaction(bool issue, const TxContext& context)
        : AssetTransaction{ context }
        , _issue(issue)
    {
    }

    void AssetIssueTransaction::UpdateImpl()
    {
        if (!AssetTransaction::BaseUpdate())
            return;

        if (!_builder)
            _builder = std::make_shared<MyBuilder>(*this);
        auto& builder = *_builder;

        if (GetState() == State::Initial)
        {
            LOG_INFO()
                << GetTxID()
                << " "
                << (_issue ? "Generating" : "Consuming")
                << " asset with owner id " << builder.m_pidAsset
                << ". Amount: " << PrintableAmount(builder.m_Value, false, kASSET, kAGROTH);

            UpdateTxDescription(TxStatus::InProgress);
        }

        if (builder.m_Coins.IsEmpty())
        {
            auto pInfo = GetWalletDB()->findAsset(builder.m_pidAsset);
            if (!pInfo)
            {
                Height h = 0;
                GetParameter(TxParameterID::AssetUnconfirmedHeight, h);
                if (h)
                    OnFailed(TxFailureReason::AssetConfirmFailed);
                else
                {
                    h = 0;
                    GetParameter(TxParameterID::AssetConfirmedHeight, h);
                    if (!h)
                    {
                        SetState(State::AssetCheck);
                        GetGateway().confirm_asset(GetTxID(), _builder->m_pidAsset, kDefaultSubTxID);
                    }
                }

                return;
            }

            WalletAsset& wa = *pInfo;
            SetParameter(TxParameterID::AssetID, wa.m_ID);
            SetParameter(TxParameterID::AssetInfoFull, Cast::Down<Asset::Full>(wa));
            SetParameter(TxParameterID::AssetConfirmedHeight, wa.m_RefreshHeight);

            SetState(State::Making);

            builder.MakeInputsAndChange(builder.m_Fee, 0);

            if (_issue)
            {
                CoinID cid;
                cid.m_Value = builder.m_Value;
                cid.m_AssetID = wa.m_ID;
                cid.m_Type = Key::Type::Regular;
                builder.CreateAddNewOutput(cid);
            }
            else
                builder.MakeInputsAndChange(builder.m_Value, wa.m_ID);

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

    bool AssetIssueTransaction::IsInSafety() const
    {
        State txState = GetState();
        return txState == State::KernelConfirmation;
    }
}
