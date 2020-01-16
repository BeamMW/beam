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

#include "push_tx_builder.h"

#include "core/shielded.h"

namespace beam::wallet::lelantus
{
    PushTxBuilder::PushTxBuilder(BaseTransaction& tx, SubTxID subTxID, const AmountList& amount, Amount fee)
        : BaseTxBuilder(tx, subTxID, amount, fee)
    {

    }

    Transaction::Ptr PushTxBuilder::CreateTransaction()
    {
        // create transaction
        auto transaction = std::make_shared<Transaction>();

        transaction->m_vInputs = move(m_Inputs);
        //transaction->m_Offset = m_Offset;
        {
            ECC::Scalar::Native offset = Zero;

            for (auto id : m_InputCoins)
            {
                ECC::Scalar::Native k;
                ECC::Point comm;
                SwitchCommitment().Create(k, comm, *m_Tx.GetWalletDB()->get_MasterKdf(), id);

                offset += k;
            }

            transaction->m_Offset = offset;
        }

        {
            TxKernelShieldedOutput::Ptr pKrn(new TxKernelShieldedOutput);
            pKrn->m_Height.m_Min = m_Tx.GetWalletDB()->getCurrentHeight();
            pKrn->m_Fee = GetFee();

            ShieldedTxo::Viewer viewer;
            viewer.FromOwner(*m_Tx.GetWalletDB()->get_MasterKdf());

            ShieldedTxo::Data::SerialParams sp;
            sp.Generate(pKrn->m_Txo.m_Serial, viewer, 13U);

            pKrn->UpdateMsg();
            ECC::Oracle oracle;
            oracle << pKrn->m_Msg;

            ShieldedTxo::Data::OutputParams op;
            op.m_Sender = m_Tx.GetMandatoryParameter<WalletID>(TxParameterID::MyID).m_Pk;
            //op.m_Message = m_Shielded.m_Message;
            op.m_Value = GetAmount();
            op.Generate(pKrn->m_Txo, oracle, viewer, 18U);

            pKrn->MsgToID();

            //m_Shielded.m_sk = sp.m_pK[0];
            //m_Shielded.m_sk += op.m_k;
            //m_Shielded.m_SerialPub = pKrn->m_Txo.m_Serial.m_SerialPub;

            /*Key::IKdf::Ptr pSerPrivate;
            ShieldedTxo::Viewer::GenerateSerPrivate(pSerPrivate, *m_WalletDB->get_MasterKdf());
            pSerPrivate->DeriveKey(m_Shielded.m_skSpendKey, sp.m_SerialPreimage);

            ECC::Point::Native pt;*/
            //pKrn->UpdateID();
            m_Tx.SetParameter(TxParameterID::KernelID, pKrn->m_Internal.m_ID, m_SubTxID);

            ECC::Point::Native pt;
            assert(pKrn->IsValid(m_Tx.GetWalletDB()->getCurrentHeight(), pt));

            transaction->m_vKernels.push_back(std::move(pKrn));

            ECC::Scalar::Native offset = transaction->m_Offset;
            offset -= op.m_k;
            transaction->m_Offset = offset;
        }

        transaction->Normalize();

        return transaction;
    }
} // namespace beam::wallet::lelantus