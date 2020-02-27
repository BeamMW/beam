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
    PushTxBuilder::PushTxBuilder(BaseTransaction& tx, const AmountList& amount, Amount fee)
        : BaseLelantusTxBuilder(tx, amount, fee)
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

        Key::IKdf::Ptr pMasterKdf = m_Tx.get_MasterKdfStrict();
        {
            ECC::Scalar::Native offset = Zero;

            for (auto id : GetInputCoins())
            {
                ECC::Scalar::Native sk;
                CoinID::Worker(id).Create(sk, *id.get_ChildKdf(pMasterKdf));
                offset += sk;
            }

            transaction->m_Offset = offset;
        }

        {
            ECC::Scalar::Native offset = transaction->m_Offset;

            for (auto id : GetOutputCoins())
            {
                ECC::Scalar::Native sk;
                CoinID::Worker(id).Create(sk, *id.get_ChildKdf(pMasterKdf));
                offset -= sk;
            }

            transaction->m_Offset = offset;
        }

        {
            TxKernelShieldedOutput::Ptr pKrn(new TxKernelShieldedOutput);
            pKrn->m_Height.m_Min = GetMinHeight();
            pKrn->m_Height.m_Max = GetMaxHeight();
            pKrn->m_Fee = GetFee();

            ShieldedTxo::Viewer viewer;
            viewer.FromOwner(*m_Tx.GetWalletDB()->get_MasterKdf());

            ShieldedTxo::Data::Params sdp;
            ECC::uintBig serialNonce;
            ECC::GenRandom(serialNonce);

            sdp.m_Serial.Generate(pKrn->m_Txo.m_Serial, viewer, serialNonce);

            pKrn->UpdateMsg();
            ECC::Oracle oracle;
            oracle << pKrn->m_Msg;

            ECC::uintBig outputNonce;
            ECC::GenRandom(outputNonce);

            sdp.m_Output.m_Sender = m_Tx.GetMandatoryParameter<WalletID>(TxParameterID::MyID).m_Pk;
            // TODO: add ShieldedMessage if needed
            // op.m_Message = m_Tx.GetMandatoryParameter<WalletID>(TxParameterID::ShieldedMessage);
            sdp.m_Output.m_Value = GetAmount();
            sdp.Generate(pKrn->m_Txo, oracle, viewer, outputNonce);

            // save shielded Coin
            ShieldedCoin shieldedCoin;
            shieldedCoin.m_value = GetAmount();
            shieldedCoin.m_createTxId = m_Tx.GetTxID();
            shieldedCoin.m_skSerialG = sdp.m_Serial.m_pK[0];
            shieldedCoin.m_skOutputG = sdp.m_Output.m_k;
            shieldedCoin.m_isCreatedByViewer = sdp.m_Serial.m_IsCreatedByViewer;
            shieldedCoin.m_sender = sdp.m_Output.m_Sender;
            shieldedCoin.m_message = sdp.m_Output.m_Message;

            m_Tx.GetWalletDB()->saveShieldedCoin(shieldedCoin);
            m_Tx.SetParameter(TxParameterID::ShieldedSerialPub, pKrn->m_Txo.m_Serial.m_SerialPub);

            // save KernelID
            pKrn->MsgToID();
            m_Tx.SetParameter(TxParameterID::KernelID, pKrn->m_Internal.m_ID);

            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << pKrn->m_Height.m_Min
                << ", max height: " << pKrn->m_Height.m_Max;

            transaction->m_vKernels.push_back(std::move(pKrn));

            ECC::Scalar::Native offset = transaction->m_Offset;
            offset -= sdp.m_Output.m_k;
            transaction->m_Offset = offset;
        }

        transaction->Normalize();

        return transaction;
    }
} // namespace beam::wallet::lelantus