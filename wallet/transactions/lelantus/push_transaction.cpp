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

            LOG_INFO()
                 << m_Context << " Sending to shielded pool "
                 << PrintableAmount(builder.m_Value, false, builder.m_AssetID ? kAmountASSET : "", builder.m_AssetID ? kAmountAGROTH : "")
                 << " (fee: " << PrintableAmount(builder.m_Fee) << ")";

            if (builder.m_AssetID)
            {
                builder.MakeInputsAndChange(builder.m_Value, builder.m_AssetID);
                builder.MakeInputsAndChange(builder.m_Fee, 0);
            }
            else
                builder.MakeInputsAndChange(builder.m_Value + builder.m_Fee, 0);

            builder.SaveCoins();
        }

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegisteredInternal, nRegistered)
            || nRegistered == proto::TxStatus::Unspecified)
        {
            if (CheckExpired())
            {
                return;
            }

            builder.GenerateInOuts();
            builder.SignSendShielded();

            if (builder.IsGeneratingInOuts() || (BaseTxBuilder::Stage::Done != builder.m_Signing))
                return;

            GetGateway().register_tx(GetTxID(), builder.m_pTransaction, GetSubTxID());
            return;
        }

/*        if (proto::TxStatus::InvalidContext == nRegistered ||   // we have to ensure that this transaction hasn't already added to blockchain
            proto::TxStatus::InvalidInput == nRegistered)       // transaction could be sent to node previously
        {
            Height lastUnconfirmedHeight = 0;
            if (GetParameter(TxParameterID::KernelUnconfirmedHeight, lastUnconfirmedHeight) && lastUnconfirmedHeight > 0)
            {
                ShieldedVoucherList vouchers;
                if (proto::TxStatus::InvalidContext == nRegistered
                    && GetParameter(TxParameterID::UnusedShieldedVoucherList, vouchers))
                {
                    if (!vouchers.empty())
                    {
                        // reset transaction state and try another vouchers left
                        SetParameter(TxParameterID::KernelUnconfirmedHeight, Height(0));
                        SetParameter(TxParameterID::Kernel, TxKernelShieldedOutput::Ptr());
                        builder.ResetKernelID();

                        const auto internalStatus = proto::TxStatus::Unspecified;
                        SetParameter(TxParameterID::TransactionRegisteredInternal, internalStatus);

                        UpdateAsync();
                    }
                    else
                    {
                        OnFailed(TxFailureReason::NoVouchers);
                    }
                    return;
                }

                OnFailed(TxFailureReason::FailedToRegister, true);
                return;
            }
        }
        else */if (proto::TxStatus::Ok != nRegistered)
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
            return;
        }

        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            ECC::Hash::Value hv;
            if (GetParameter(TxParameterID::KernelID, hv))
                ConfirmKernel(hv);
            return;
        }

        // getProofShieldedOutp
        if (m_waitingShieldedProof)
        {
            ECC::Point serialPub = GetMandatoryParameter<ECC::Point>(TxParameterID::ShieldedSerialPub);

            GetGateway().get_proof_shielded_output(GetTxID(), serialPub, [this, weak = this->weak_from_this()](proto::ProofShieldedOutp proof)
                {
                    if (weak.expired())
                    {
                        return;
                    }

                    if (m_waitingShieldedProof)
                    {
                        m_waitingShieldedProof = false;

                        // update shielded output
                        auto coin = GetWalletDB()->getShieldedCoin(GetTxID());
                        if (coin) // payment to ourself
                        {
                            coin->m_TxoID = proof.m_ID;
                            coin->m_confirmHeight = std::min(coin->m_confirmHeight, proof.m_Height);

                            // save shielded output to DB
                            GetWalletDB()->saveShieldedCoin(*coin);
                        }
                    }
                    UpdateAsync();
                });
            return;
        }

        SetCompletedTxCoinStatuses(hProof);

        CompleteTx();
    }
    
    void PushTransaction::RollbackTx()
    {
        LOG_INFO() << m_Context << " Transaction failed. Rollback...";
        GetWalletDB()->rollbackTx(GetTxID());
        GetWalletDB()->deleteShieldedCoinsCreatedByTx(GetTxID());
    }

    bool PushTransaction::IsSelfTx() const
    {
        WalletID peerID;
        if (GetParameter(TxParameterID::PeerID, peerID))
        {
            auto address = GetWalletDB()->getAddress(peerID);
            return address.is_initialized() && address->isOwn();
        }
        ShieldedVoucherList vouchers;
        return !GetParameter(TxParameterID::ShieldedVoucherList, vouchers); // if we have vouchers then this is not self tx
    }
} // namespace beam::wallet::lelantus