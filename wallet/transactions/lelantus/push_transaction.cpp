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

#include "core/proto.h"
#include "core/shielded.h"

#include "push_tx_builder.h"

namespace beam::wallet::lelantus
{
    TxParameters CreatePushTransactionParameters(const WalletID& myID, const boost::optional<TxID>& txId)
    {
        return CreateTransactionParameters(TxType::PushTransaction, txId)
            .SetParameter(TxParameterID::MyID, myID);
    }

    //ShieldedTxo::Voucher CreateNewVoucher(IWalletDB::Ptr db, const ECC::Scalar& sk)
    //{
    //    ShieldedTxo::Voucher voucher;
    //    ShieldedTxo::Viewer viewer;
    //    const Key::Index nIdx = 0;
    //    viewer.FromOwner(*db->get_OwnerKdf(), nIdx);
    //
    //    ECC::GenRandom(voucher.m_SharedSecret); // not yet, just a nonce placeholder
    //
    //    ShieldedTxo::Data::TicketParams tp;
    //    tp.Generate(voucher.m_Ticket, viewer, voucher.m_SharedSecret);
    //
    //    voucher.m_SharedSecret = tp.m_SharedSecret;
    //
    //    ECC::Hash::Value hv;
    //    voucher.get_Hash(hv);
    //
    //    voucher.m_Signature.Sign(hv, sk);
    //
    //    return voucher;
    //}

    BaseTransaction::Ptr PushTransaction::Creator::Create(INegotiatorGateway& gateway
        , IWalletDB::Ptr walletDB
        , const TxID& txID)
    {
        return BaseTransaction::Ptr(new PushTransaction(gateway, walletDB, txID));
    }

    TxParameters PushTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        // TODO roman.strilets implement this
        return parameters;
    }

    PushTransaction::PushTransaction(INegotiatorGateway& gateway
        , IWalletDB::Ptr walletDB
        , const TxID& txID)
        : BaseTransaction(gateway, walletDB, txID)
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
        AmountList amoutList;
        if (!GetParameter(TxParameterID::AmountList, amoutList))
        {
            amoutList = AmountList{ GetMandatoryParameter<Amount>(TxParameterID::Amount) };
        }

        if (!m_TxBuilder)
        {
            m_TxBuilder = std::make_shared<PushTxBuilder>(*this, amoutList, GetMandatoryParameter<Amount>(TxParameterID::Fee));
        }

        if (!m_TxBuilder->GetInitialTxParams())
        {
            UpdateTxDescription(TxStatus::InProgress);

            LOG_INFO() << GetTxID() << " Sending to shielded pool "
                << PrintableAmount(m_TxBuilder->GetAmount())
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

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
        {
            if (CheckExpired())
            {
                return;
            }

            // Construct transaction
            auto transaction = m_TxBuilder->CreateTransaction();

            // Verify final transaction
            TxBase::Context::Params pars;
            TxBase::Context ctx(pars);
            ctx.m_Height.m_Min = m_TxBuilder->GetMinHeight();
            if (!transaction->IsValid(ctx))
            {
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return;
            }

            GetGateway().register_tx(GetTxID(), transaction);
            return;
        }

        if (proto::TxStatus::InvalidContext == nRegistered ||   // we have to ensure that this transaction hasn't already added to blockchain
            proto::TxStatus::InvalidInput == nRegistered)       // transaction could be sent to node previously
        {
            Height lastUnconfirmedHeight = 0;
            if (GetParameter(TxParameterID::KernelUnconfirmedHeight, lastUnconfirmedHeight) && lastUnconfirmedHeight > 0)
            {
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
        LOG_INFO() << GetTxID() << " Transaction failed. Rollback...";
        GetWalletDB()->rollbackTx(GetTxID());
        GetWalletDB()->deleteShieldedCoinsCreatedByTx(GetTxID());
    }
} // namespace beam::wallet::lelantus