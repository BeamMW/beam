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
        : BaseTransaction(TxType::PullTransaction, context)
    {
    }

    bool PullTransaction::IsInSafety() const
    {
        // TODO roman.strilets implement this
        return true;
    }

    struct PullTransaction::MyBuilder
        :public SimpleTxBuilder
    {
        using SimpleTxBuilder::SimpleTxBuilder;
    };

    void PullTransaction::UpdateImpl()
    {
        if (!m_TxBuilder)
            m_TxBuilder = std::make_shared<MyBuilder>(*this, GetSubTxID());

        auto& builder = *m_TxBuilder;

        if (builder.m_Coins.IsEmpty())
        {
            UpdateTxDescription(TxStatus::InProgress);

            auto shieldedId = GetMandatoryParameter<TxoID>(TxParameterID::ShieldedOutputId);
            auto shieldedCoin = GetWalletDB()->getShieldedCoin(shieldedId);

            if (!shieldedCoin || shieldedCoin->m_Status != ShieldedCoin::Status::Available)
                throw TransactionFailedException(false, TxFailureReason::NoInputs);

            ShieldedCoin& sc = *shieldedCoin;
            Asset::ID aid = sc.m_CoinID.m_AssetID;

            const auto unitName = aid ? kAmountASSET : "";
            const auto nthName = aid ? kAmountAGROTH : "";

            LOG_INFO() << m_Context << " Extracting from shielded pool:"
                << " ID - " << shieldedId << ", amount - " << PrintableAmount(sc.m_CoinID.m_Value, false, unitName, nthName)
                << ", receiving amount - " << PrintableAmount(sc.m_CoinID.m_Value, false, unitName, nthName)
                << " (fee: " << PrintableAmount(builder.m_Fee) << ")";

            IPrivateKeyKeeper2::ShieldedInput si;
            Cast::Down<ShieldedTxo::ID>(si) = sc.m_CoinID;
            si.m_Fee = Transaction::FeeSettings::get(builder.m_Height.m_Min).m_ShieldedInputTotal;

            BaseTxBuilder::Balance bb(builder);

            bb.m_Map[0].m_Value -= builder.m_Fee;
            bb.Add(si);

            bb.CompleteBalance();

            builder.SaveCoins();
        }

        if (!builder.SignTx())
            return;

        builder.FinalyzeTx();

        State state = State::Initial;
        GetParameter(TxParameterID::State, state, GetSubTxID());

        if (State::Initial == state)
        {
            builder.m_pTransaction->Normalize();
            builder.VerifyTx();

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