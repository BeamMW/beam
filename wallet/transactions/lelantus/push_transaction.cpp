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

#include "proto.h"
#include "core/shielded.h"

#include "push_tx_builder.h"

namespace beam::wallet::lelantus
{
    BaseTransaction::Ptr PushTransaction::Creator::Create(INegotiatorGateway& gateway
        , IWalletDB::Ptr walletDB
        , IPrivateKeyKeeper::Ptr keyKeeper
        , const TxID& txID)
    {
        return BaseTransaction::Ptr(new PushTransaction(gateway, walletDB, keyKeeper, txID));
    }

    TxParameters PushTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        // TODO roman.strilets implement this
        return parameters;
    }

    PushTransaction::PushTransaction(INegotiatorGateway& gateway
        , IWalletDB::Ptr walletDB
        , IPrivateKeyKeeper::Ptr keyKeeper
        , const TxID& txID)
        : BaseTransaction(gateway, walletDB, keyKeeper, txID)
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
        //State txState = GetState();
        AmountList amoutList;
        if (!GetParameter(TxParameterID::AmountList, amoutList))
        {
            amoutList = AmountList{ GetMandatoryParameter<Amount>(TxParameterID::Amount) };
        }

        if (!m_TxBuilder)
        {
            m_TxBuilder = std::make_shared<PushTxBuilder>(*this, kDefaultSubTxID, amoutList, GetMandatoryParameter<Amount>(TxParameterID::Fee));
        }

        if (!m_TxBuilder->GetInitialTxParams())
        {
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
            // TODO check expired

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
            //SetState(State::Registration);
            return;
        }

        if (proto::TxStatus::InvalidContext == nRegistered) // we have to ensure that this transaction hasn't already added to blockchain)
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
            //SetState(State::KernelConfirmation);
            ConfirmKernel(m_TxBuilder->GetKernelID());
            return;
        }

        SetCompletedTxCoinStatuses(hProof);

        CompleteTx();

        // create kernel
        //{
        //    TxKernelShieldedOutput::Ptr pKrn(new TxKernelShieldedOutput);
        //    pKrn->m_Height.m_Min = m_WalletDB->getCurrentHeight();

        //    ShieldedTxo::Viewer viewer;
        //    viewer.FromOwner(*m_WalletDB->get_MasterKdf());

        //    ShieldedTxo::Data::SerialParams sp;
        //    sp.Generate(pKrn->m_Txo.m_Serial, viewer, 13U);

        //    pKrn->UpdateMsg();
        //    ECC::Oracle oracle;
        //    oracle << pKrn->m_Msg;

        //    ShieldedTxo::Data::OutputParams op;
        //    op.m_Sender = GetMandatoryParameter<WalletID>(TxParameterID::MyID).m_Pk;
        //    //op.m_Message = m_Shielded.m_Message;
        //    op.m_Value = GetMandatoryParameter<Amount>(TxParameterID::Amount);
        //    op.Generate(pKrn->m_Txo, oracle, viewer, 18U);

        //    pKrn->MsgToID();

        //    //m_Shielded.m_sk = sp.m_pK[0];
        //    //m_Shielded.m_sk += op.m_k;
        //    //m_Shielded.m_SerialPub = pKrn->m_Txo.m_Serial.m_SerialPub;

        //    /*Key::IKdf::Ptr pSerPrivate;
        //    ShieldedTxo::Viewer::GenerateSerPrivate(pSerPrivate, *m_WalletDB->get_MasterKdf());
        //    pSerPrivate->DeriveKey(m_Shielded.m_skSpendKey, sp.m_SerialPreimage);

        //    ECC::Point::Native pt;*/

        //    ECC::Point::Native pt;
        //    assert(pKrn->IsValid(m_WalletDB->getCurrentHeight(), pt));

        //    //msgTx.m_Transaction->m_vKernels.push_back(std::move(pKrn));
        //}
    }
} // namespace beam::wallet::lelantus