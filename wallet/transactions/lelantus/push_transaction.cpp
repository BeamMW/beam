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
        return BaseTransaction::Ptr(new PushTransaction(context, m_withAssets));
    }

    TxParameters PushTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        wallet::TestSenderAddress(parameters, m_walletDB);

        return wallet::ProcessReceiverAddress(parameters, m_walletDB);
    }

    PushTransaction::PushTransaction(const TxContext& context
        , bool withAssets)
        : BaseTransaction(context)
        , m_withAssets(withAssets)
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
        const bool isSelfTx = IsSelfTx();
        AmountList amoutList;
        if (!GetParameter(TxParameterID::AmountList, amoutList))
        {
            amoutList = AmountList{ GetMandatoryParameter<Amount>(TxParameterID::Amount) };
        }

        if (!m_TxBuilder)
        {
            m_TxBuilder = std::make_shared<PushTxBuilder>(*this, amoutList, GetMandatoryParameter<Amount>(TxParameterID::Fee), m_withAssets);
        }

        if (!m_TxBuilder->GetInitialTxParams())
        {
            UpdateTxDescription(TxStatus::InProgress);

            const bool isAsset = m_TxBuilder->IsAssetTx();
            LOG_INFO()
                 << m_Context << " Sending to shielded pool "
                 << PrintableAmount(m_TxBuilder->GetAmount(), false, isAsset ? kAmountASSET : "", isAsset ? kAmountAGROTH : "")
                 << " (fee: " << PrintableAmount(m_TxBuilder->GetFee()) << ")";

            m_TxBuilder->SelectInputs();
            m_TxBuilder->AddChange();
        }

        if (m_TxBuilder->CreateInputs())
        {
            return;
        }

        if (m_TxBuilder->CreateOutputs())
        {
            return;
        }
        
        {
            ShieldedVoucherList vouchers;
            if (!isSelfTx && !GetParameter(TxParameterID::ShieldedVoucherList, vouchers))
            {
                boost::optional<ShieldedTxo::Voucher> voucher;
                GetGateway().get_UniqueVoucher(GetMandatoryParameter<WalletID>(TxParameterID::PeerID), GetTxID(), voucher);

                if (!voucher)
                    return;

                SetParameter(TxParameterID::ShieldedVoucherList, ShieldedVoucherList(1, *voucher));
            }
        }

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegisteredInternal, nRegistered)
            || nRegistered == proto::TxStatus::Unspecified)
        {
            if (CheckExpired())
            {
                return;
            }

            // Construct transaction
            auto transaction = m_TxBuilder->CreateTransaction();
            if (!transaction)
            {
                OnFailed(TxFailureReason::NoVouchers);
                return;
            }

            // Verify final transaction
            TxBase::Context::Params pars;
            TxBase::Context ctx(pars);
            ctx.m_Height.m_Min = m_TxBuilder->GetMinHeight();
            if (!transaction->IsValid(ctx))
            {
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return;
            }

            GetGateway().register_tx(GetTxID(), transaction, GetSubTxID());
            return;
        }

        if (proto::TxStatus::InvalidContext == nRegistered ||   // we have to ensure that this transaction hasn't already added to blockchain
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
                        m_TxBuilder->ResetKernelID();

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
        else if (proto::TxStatus::Ok != nRegistered)
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
            return;
        }

        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            ConfirmKernel(m_TxBuilder->GetKernelID());
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
                            coin->m_ID = proof.m_ID;
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
        WalletID peerID = GetMandatoryParameter<WalletID>(TxParameterID::PeerID);
        auto address = GetWalletDB()->getAddress(peerID);
        return address.is_initialized() && address->isOwn();
    }
} // namespace beam::wallet::lelantus