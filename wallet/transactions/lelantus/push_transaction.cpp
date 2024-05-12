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
    TxParameters CreatePushTransactionParameters(const boost::optional<TxID>& txId)
    {
        return CreateTransactionParameters(TxType::PushTransaction, txId);
    }

    BaseTransaction::Ptr PushTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new PushTransaction(context));
    }

    TxParameters PushTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        auto walletDB = m_dbFunc();

        auto receiverID = parameters.GetParameter<WalletID>(TxParameterID::PeerAddr);
        if (receiverID)
        {
            auto vouchers = parameters.GetParameter<ShieldedVoucherList>(TxParameterID::ShieldedVoucherList);
            if (vouchers)
            {
                storage::SaveVouchers(*walletDB, *vouchers, *receiverID);
            }
        }

        const auto& originalToken = parameters.GetParameter<std::string>(TxParameterID::OriginalToken);
        if (originalToken)
        {
            auto addr = walletDB->getAddressByToken(*originalToken);
            if (addr && addr->isOwn())
            {
                TxParameters temp{ parameters };
                temp.SetParameter(TxParameterID::IsSelfTx, addr->isOwn());
                return wallet::ProcessReceiverAddress(temp, walletDB);
            }
        }
        return wallet::ProcessReceiverAddress(parameters, walletDB);
    }

    PushTransaction::PushTransaction(const TxContext& context)
        : BaseTransaction(TxType::PushTransaction, context)
    {
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

        if (MaxHeight == builder.m_Height.m_Max) {
            uint32_t lifetime = 0;
            Block::SystemState::Full s;
            if (GetParameter(TxParameterID::Lifetime, lifetime) && lifetime > 0 && GetTip(s)) {
                builder.m_Height.m_Max = s.m_Height + lifetime;
                BEAM_LOG_DEBUG() << "Setup max height for PushTransaction = " << builder.m_Height.m_Max;
                SetParameter(TxParameterID::MaxHeight, builder.m_Height.m_Max, GetSubTxID());
            }
        }

        Height maxHeight = builder.m_Height.m_Max;

        if (builder.m_Coins.IsEmpty())
        {
            UpdateTxDescription(TxStatus::InProgress);

            BaseTxBuilder::Balance bb(builder);
            bb.m_Map[0].m_Value -= builder.m_Fee;
            bb.m_Map[builder.m_AssetID].m_Value -= builder.m_Value;

            bb.CompleteBalance();

            BEAM_LOG_INFO()
                 << m_Context << " Sending to shielded pool "
                 << PrintableAmount(builder.m_Value, false, builder.m_AssetID)
                 << " (fee: " << PrintableAmount(builder.m_Fee) << ")";

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
            GetGateway().register_tx(GetTxID(), builder.m_pTransaction, nullptr, GetSubTxID());
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
            m_OutpHeight = maxHeight;
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
            });
        }

        if (maxHeight == m_OutpHeight)
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
        BEAM_LOG_INFO() << m_Context << " Transaction failed. Rollback...";
        m_Context.GetWalletDB()->restoreCoinsSpentByTx(GetTxID());
        m_Context.GetWalletDB()->deleteCoinsCreatedByTx(GetTxID());
        GetWalletDB()->deleteShieldedCoinsCreatedByTx(GetTxID());
    }

} // namespace beam::wallet::lelantus