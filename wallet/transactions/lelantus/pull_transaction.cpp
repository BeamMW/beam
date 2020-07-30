// Copyright 2020 The Beam Team
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

#include "pull_transaction.h"
#include "core/shielded.h"
#include "wallet/core/strings_resources.h"

namespace beam::wallet::lelantus
{
    TxParameters CreatePullTransactionParameters(const WalletID& myID, const boost::optional<TxID>& txId)
    {
        return CreateTransactionParameters(TxType::PullTransaction, txId)
            .SetParameter(TxParameterID::MyID, myID)
            .SetParameter(TxParameterID::IsSender, false);
    }

    BaseTransaction::Ptr PullTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new PullTransaction(context));
    }

    TxParameters PullTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        // TODO roman.strilets implement this
        return parameters;
    }

    PullTransaction::PullTransaction(const TxContext& context)
        : BaseTransaction(context)
    {
    }

    TxType PullTransaction::GetType() const
    {
        return TxType::PullTransaction;
    }

    bool PullTransaction::IsInSafety() const
    {
        // TODO roman.strilets implement this
        return true;
    }

    struct PullTransaction::MyBuilder
        :public BaseTxBuilder
    {
        TxKernelStd* m_pKrn = nullptr;

        void RefreshKrn()
        {
            TxKernelStd::Ptr pKrn;
            m_Tx.GetParameter(TxParameterID::Kernel, pKrn, m_SubTxID);

            if (pKrn)
            {
                m_pKrn = pKrn.get();
                m_pTransaction->m_vKernels.push_back(std::move(pKrn));
            }
        }

        MyBuilder(BaseTransaction& tx, SubTxID subTxID)
            :BaseTxBuilder(tx, subTxID)
        {
            RefreshKrn();

            if (m_pKrn)
            {
                m_Signing = Stage::Done;
                m_pTransaction->Normalize();
            }
        }
    };

    void PullTransaction::UpdateImpl()
    {
        Transaction::FeeSettings fs;
        Amount feeShielded = fs.m_ShieldedInput + fs.m_Kernel;

        if (!m_TxBuilder)
        {
            m_TxBuilder = std::make_shared<MyBuilder>(*this, GetSubTxID());

            // by convention the fee now includes ALL the fee, whereas our code will add the minimal shielded fee.

            if (m_TxBuilder->m_Fee >= feeShielded)
                m_TxBuilder->m_Fee -= feeShielded;
            std::setmax(m_TxBuilder->m_Fee, fs.m_Kernel);
        }

        auto& builder = *m_TxBuilder;

        if (builder.m_Coins.IsEmpty())
        {
            UpdateTxDescription(TxStatus::InProgress);

            TxoID shieldedId = GetMandatoryParameter<TxoID>(TxParameterID::ShieldedOutputId);
            auto shieldedCoin = GetWalletDB()->getShieldedCoin(shieldedId);

            if (!shieldedCoin || !shieldedCoin->IsAvailable())
                throw TransactionFailedException(false, TxFailureReason::NoInputs);

            ShieldedCoin& sc = *shieldedCoin;
            Asset::ID aid = sc.m_CoinID.m_AssetID;

            const auto unitName = aid ? kAmountASSET : "";
            const auto nthName = aid ? kAmountAGROTH : "";

            LOG_INFO() << m_Context << " Extracting from shielded pool:"
                << " ID - " << shieldedId << ", amount - " << PrintableAmount(sc.m_CoinID.m_Value, false, unitName, nthName)
                << ", receiving amount - " << PrintableAmount(sc.m_CoinID.m_Value, false, unitName, nthName)
                << " (fee: " << PrintableAmount(builder.m_Fee) << ")";

            auto& vInp = builder.m_Coins.m_InputShielded;
            Cast::Down<ShieldedTxo::ID>(vInp.emplace_back()) = sc.m_CoinID;
            vInp.back().m_Fee = feeShielded;
            builder.m_Balance.Add(vInp.back());

            sc.m_spentTxId = GetTxID();
            GetWalletDB()->saveShieldedCoin(sc);

            if (aid)
                builder.MakeInputsAndChange(0, aid);

            builder.MakeInputsAndChange(builder.m_Fee, 0);
            builder.SaveCoins();
        }

        builder.GenerateInOuts();

        if (!builder.m_pKrn)
        {
            builder.SignSplit();
            if (BaseTxBuilder::Stage::Done == builder.m_Signing)
                builder.RefreshKrn();

            if (!builder.m_pKrn)
                return;
        }

        if (m_TxBuilder->IsGeneratingInOuts())
            return;

        State state = State::Initial;
        GetParameter(TxParameterID::State, state, GetSubTxID());

        if (State::Initial == state)
        {
            builder.m_pTransaction->Normalize();
            if (!builder.VerifyTx())
            {
                OnFailed(TxFailureReason::InvalidTransaction);
                return;
            }

            SetState(State::Registration);
            state = State::Registration;
        }

        if (State::Registration == state)
        {
            uint8_t nRegistered = proto::TxStatus::Unspecified;
            if (GetParameter(TxParameterID::TransactionRegistered, nRegistered))
            {
                if (proto::TxStatus::Ok != nRegistered)
                {
                    OnFailed(TxFailureReason::FailedToRegister, true);
                    return;
                }

                SetState(State::KernelConfirmation);
            }
            else
            {
                if (CheckExpired())
                    return;

                // register TX
                GetGateway().register_tx(GetTxID(), builder.m_pTransaction, GetSubTxID());
                return;
            }
        }

        // get Kernel proof
        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            if (!CheckExpired())
                ConfirmKernel(builder.m_pKrn->m_Internal.m_ID);
            return;
        }

        SetCompletedTxCoinStatuses(hProof);
        CompleteTx();
    }

    void PullTransaction::RollbackTx()
    {
        LOG_INFO() << m_Context << " Transaction failed. Rollback...";
        GetWalletDB()->restoreShieldedCoinsSpentByTx(GetTxID());
        GetWalletDB()->deleteCoinsCreatedByTx(GetTxID());
    }
} // namespace beam::wallet::lelantus