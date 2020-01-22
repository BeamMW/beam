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

        transaction->m_vInputs = m_Tx.GetMandatoryParameter<std::vector<Input::Ptr>>(TxParameterID::Inputs);

        {
            std::vector<Output::Ptr> outputs;
            if (m_Tx.GetParameter(TxParameterID::Outputs, outputs))
            {
                transaction->m_vOutputs = std::move(outputs);
            }
        }
        //transaction->m_Offset = m_Offset;
        {
            ECC::Scalar::Native offset = Zero;

            for (auto id : GetInputCoins())
            {
                ECC::Scalar::Native k;
                ECC::Point comm;
                CoinID::Worker(id).Create(k, comm, *m_Tx.GetWalletDB()->get_MasterKdf());

                offset += k;
            }

            transaction->m_Offset = offset;
        }

        {
            ECC::Scalar::Native offset = transaction->m_Offset;

            for (auto id : GetOutputCoins())
            {
                ECC::Scalar::Native k;
                ECC::Point comm;
                CoinID::Worker(id).Create(k, comm, *m_Tx.GetWalletDB()->get_MasterKdf());

                offset -= k;
            }

            transaction->m_Offset = offset;
        }

        {
            TxKernelShieldedOutput::Ptr pKrn(new TxKernelShieldedOutput);
            pKrn->m_Height.m_Min = m_Tx.GetWalletDB()->getCurrentHeight();
            pKrn->m_Fee = GetFee();

            ShieldedTxo::Viewer viewer;
            viewer.FromOwner(*m_Tx.GetWalletDB()->get_MasterKdf());

            ECC::uintBig serialNonce;
            ECC::GenRandom(serialNonce);
            ShieldedTxo::Data::SerialParams sp;
            sp.Generate(pKrn->m_Txo.m_Serial, viewer, serialNonce);

            pKrn->UpdateMsg();
            ECC::Oracle oracle;
            oracle << pKrn->m_Msg;

            ECC::uintBig outputNonce;
            ECC::GenRandom(outputNonce);
            ShieldedTxo::Data::OutputParams op;
            op.m_Sender = m_Tx.GetMandatoryParameter<WalletID>(TxParameterID::MyID).m_Pk;
            //op.m_Message = m_Shielded.m_Message;
            op.m_Value = GetAmount();
            op.Generate(pKrn->m_Txo, oracle, viewer, outputNonce);

            // save shielded Coin
            ShieldedCoin shieldedCoin;
            shieldedCoin.m_value = GetAmount();
            shieldedCoin.m_createTxId = m_Tx.GetTxID();
            shieldedCoin.m_skSerialG = sp.m_pK[0];
            shieldedCoin.m_skOutputG = op.m_k;
            shieldedCoin.m_serialPub = pKrn->m_Txo.m_Serial.m_SerialPub;
            shieldedCoin.m_isCreatedByViewer = sp.m_IsCreatedByViewer;

            m_Tx.SetParameter(TxParameterID::ShieldedCoin, shieldedCoin);

            // save KernelID
            pKrn->MsgToID();
            m_Tx.SetParameter(TxParameterID::KernelID, pKrn->m_Internal.m_ID);

            // verify TxKernelShieldedOutput
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