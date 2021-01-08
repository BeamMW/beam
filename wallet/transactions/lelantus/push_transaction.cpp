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

#include "push_transaction.h"
#include "core/shielded.h"
#include "push_tx_builder.h"
#include "wallet/core/common_utils.h"
#include "wallet/core/strings_resources.h"
#include "wallet/core/wallet.h"

namespace beam::wallet::lelantus
{
    TxParameters CreatePushTransactionParameters(const WalletID& myID, const boost::optional<TxID>& txId)
    {
        return CreateTransactionParameters(TxType::PushTransaction, txId)
            .SetParameter(TxParameterID::MyID, myID);
    }

    BaseTransaction::Ptr PushTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new PushTransaction(context));
    }

    TxParameters PushTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        wallet::TestSenderAddress(parameters, m_walletDB);

        return wallet::ProcessReceiverAddress(parameters, m_walletDB, false);
    }

    PushTransaction::PushTransaction(const TxContext& context)
        : BaseTransaction(context)
    {
    }

    TxType PushTransaction::GetType() const
    {
        return TxType::PushTransaction;
    }

    bool PushTransaction::IsInSafety() const
    {
        // TODO roman.strilets implement this
        return true;
    }

    void PushTransaction::UpdateImpl()
    {
        if (!m_TxBuilder)
            m_TxBuilder = std::make_shared<PushTxBuilder>(*this);
        PushTxBuilder& builder = *m_TxBuilder;

        if (builder.m_Coins.IsEmpty())
        {
            UpdateTxDescription(TxStatus::InProgress);

            BaseTxBuilder::Balance bb(builder);
            bb.m_Map[0].m_Value -= builder.m_Fee;
            bb.m_Map[builder.m_AssetID].m_Value -= builder.m_Value;

            bb.CompleteBalance();

            LOG_INFO()
                 << m_Context << " Sending to shielded pool "
                 << PrintableAmount(builder.m_Value, false, builder.m_AssetID ? kAmountASSET : "", builder.m_AssetID ? kAmountAGROTH : "")
                 << " (fee: " << PrintableAmount(GetFeeWithAdditionalValueForShieldedInputs(builder)) << ")";

            builder.SaveCoins();
        }

        builder.GenerateInOuts();
        builder.SignSendShielded();

        if (builder.IsGeneratingInOuts() || !builder.m_pKrn)
            return;

        builder.FinalyzeTx();

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        GetParameter(TxParameterID::TransactionRegisteredInternal, nRegistered);
        if (nRegistered == proto::TxStatus::Unspecified)
        {
            if (CheckExpired())
            {
                return;
            }
            UpdateTxDescription(TxStatus::Registering);
            GetGateway().register_tx(GetTxID(), builder.m_pTransaction, GetSubTxID());
            return;
        }
        else if (nRegistered == proto::TxStatus::LowFee)
        {
            OnFailed(TxFailureReason::FeeIsTooSmall);
            return;
        }
        else if (nRegistered == proto::TxStatus::InvalidInput)
        {
            OnFailed(TxFailureReason::InvalidTransaction);
            return;
        }

        if (!m_OutpHeight)
        {
            m_OutpHeight = MaxHeight;
            GetGateway().get_proof_shielded_output(GetTxID(), builder.get_TxoStrict().m_Ticket.m_SerialPub, [this, weak = this->weak_from_this()](proto::ProofShieldedOutp& proof)
            {
                auto thisHolder = weak.lock();
                if (thisHolder) // not expired
                {
                    try {
                        m_OutpHeight = 0;
                        OnOutpProof(proof);
                    }
                    catch (const TransactionFailedException& ex) {
                        OnFailed(ex.GetReason(), ex.ShouldNofify());
                    }
                }
                LOG_DEBUG() << "get_proof_shielded_output exit";
            });
        }

        if (MaxHeight == m_OutpHeight)
            return;

        Height h = 0;
        GetParameter(TxParameterID::KernelProofHeight, h);
        if (h == m_OutpHeight)
        {
            auto coin = GetWalletDB()->getShieldedCoin(GetTxID());
            if (coin) // payment to ourself
            {
                coin->m_TxoID = m_OutpID;
                coin->m_confirmHeight = std::min(coin->m_confirmHeight, h);
                // save shielded output to DB
                GetWalletDB()->saveShieldedCoin(*coin);
            }

            SetCompletedTxCoinStatuses(h);
            CompleteTx();
        }
        else
        {
            GetParameter(TxParameterID::KernelUnconfirmedHeight, h);
            if (h >= m_OutpHeight)
            {
                // reset everything, try again!
                m_OutpHeight = 0;
                builder.ResetSig();
                UpdateAsync();
            }
            else
                ConfirmKernel(builder.m_pKrn->m_Internal.m_ID);
        }
    }
    
    void PushTransaction::OnOutpProof(proto::ProofShieldedOutp& p)
    {
        if (p.m_Proof.empty())
        {
            if (!CheckExpired())
                UpdateOnNextTip();
            return;
        }

        assert(m_TxBuilder);
        auto& builder = *m_TxBuilder;

        // make sure this voucher was indeed used with our output
        const ShieldedTxo& txo = builder.get_TxoStrict();

        if ((txo.m_Commitment == p.m_Commitment) && builder.m_pKrn->m_Height.IsInRange(p.m_Height))
        {
            // chances this outp is part of our kernel
            m_OutpID = p.m_ID;
            m_OutpHeight = p.m_Height;
        }
        else
        {
            if (CheckExpired())
                return;

            // reset everything, try again!
            builder.ResetSig();
        }

        Update();
    }

    void PushTransaction::RollbackTx()
    {
        LOG_INFO() << m_Context << " Transaction failed. Rollback...";
        m_Context.GetWalletDB()->restoreCoinsSpentByTx(GetTxID());
        m_Context.GetWalletDB()->deleteCoinsCreatedByTx(GetTxID());
        GetWalletDB()->deleteShieldedCoinsCreatedByTx(GetTxID());
    }

} // namespace beam::wallet::lelantus