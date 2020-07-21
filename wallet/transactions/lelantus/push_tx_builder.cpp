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
    PushTxBuilder::PushTxBuilder(BaseTransaction& tx, const AmountList& amount, Amount fee, bool withAssets)
        : BaseLelantusTxBuilder(tx, amount, fee, withAssets)
    {
    }

    Transaction::Ptr PushTxBuilder::CreateTransaction()
    {
        // create transaction
        auto transaction = std::make_shared<Transaction>();
        ECC::Scalar::Native offset(Zero);

        transaction->m_vInputs = m_Tx.GetMandatoryParameter<std::vector<Input::Ptr>>(TxParameterID::Inputs);

        {
            std::vector<Output::Ptr> outputs;
            if (m_Tx.GetParameter(TxParameterID::Outputs, outputs))
            {
                transaction->m_vOutputs = std::move(outputs);
            }
        }

        Key::IKdf::Ptr pMasterKdf = m_Tx.get_MasterKdfStrict();

        for (auto id : GetInputCoins())
        {
            ECC::Scalar::Native sk;
            CoinID::Worker(id).Create(sk, *id.get_ChildKdf(pMasterKdf));
            offset += sk;
        }

        for (auto id : GetOutputCoins())
        {
            ECC::Scalar::Native sk;
            CoinID::Worker(id).Create(sk, *id.get_ChildKdf(pMasterKdf));
            offset -= sk;
        }

        ShieldedTxo::Data::Params pars;
        pars.m_Output.m_Value = GetAmount();
        pars.m_Output.m_AssetID = GetAssetId();
        ZeroObject(pars.m_Output.m_User);

        pars.m_Output.m_User.m_Sender = m_Tx.GetMandatoryParameter<WalletID>(TxParameterID::MyID).m_Pk;
        // TODO: add ShieldedMessage if needed
        // op.m_User.m_Message = m_Tx.GetMandatoryParameter<WalletID>(TxParameterID::ShieldedMessage);

        bool bSendingToSelf = false;

        ShieldedVoucherList vouchers;
        if (!m_Tx.GetParameter(TxParameterID::UnusedShieldedVoucherList, vouchers))
        {
            if (!m_Tx.GetParameter(TxParameterID::ShieldedVoucherList, vouchers))
            {
                bSendingToSelf = true;
                // no voucher - means we're sending to ourselves. Create our voucher
                ShieldedTxo::Voucher& voucher = vouchers.emplace_back();
                ShieldedTxo::Viewer viewer;
                const Key::Index nIdx = 0;
                viewer.FromOwner(*m_Tx.GetWalletDB()->get_OwnerKdf(), nIdx);

                ECC::GenRandom(voucher.m_SharedSecret); // not yet, just a nonce placeholder

                pars.m_Ticket.Generate(voucher.m_Ticket, viewer, voucher.m_SharedSecret);

                voucher.m_SharedSecret = pars.m_Ticket.m_SharedSecret;
                ZeroObject(voucher.m_Signature);

                // save shielded Coin
                ShieldedCoin shieldedCoin;
                pars.ToID(shieldedCoin.m_CoinID);
                shieldedCoin.m_CoinID.m_Key.m_nIdx = nIdx;
                shieldedCoin.m_createTxId = m_Tx.GetTxID();

                m_Tx.GetWalletDB()->saveShieldedCoin(shieldedCoin);
            }
            m_Tx.SetParameter(TxParameterID::UnusedShieldedVoucherList, vouchers);
        }

        if (vouchers.empty())
        {
            LOG_ERROR() << "There are no vouchers to complete this transaction.";
            return {};
        }

        const ShieldedTxo::Voucher& voucher = vouchers.back();
        pars.m_Output.Restore_kG(voucher.m_SharedSecret);

        ECC::Scalar::Native kWrap;

        if (!bSendingToSelf)
        {
            ECC::Hash::Value hv;
            ECC::Hash::Processor()
                << "so.wrap"
                << voucher.m_SharedSecret
                >> hv;

            pMasterKdf->DeriveKey(kWrap, hv);
            offset -= kWrap;
        }

        TxKernel::Ptr pKrn;
        if (!m_Tx.GetParameter(TxParameterID::Kernel, pKrn) || !pKrn)
        {
            HeightRange hr = { GetMinHeight(), GetMaxHeight() };

            TxKernelShieldedOutput::Ptr pKrnOut = std::make_unique<TxKernelShieldedOutput>();
            pKrnOut->m_CanEmbed = !bSendingToSelf;
            if (bSendingToSelf)
                pKrnOut->m_Height = hr;

            pKrnOut->m_Fee = GetFee();

            pKrnOut->UpdateMsg();
            ECC::Oracle oracle;
            oracle << pKrnOut->m_Msg;

            pKrnOut->m_Txo.m_Ticket = voucher.m_Ticket;
            pars.m_Output.Generate(pKrnOut->m_Txo, voucher.m_SharedSecret, oracle);

            m_Tx.SetParameter(TxParameterID::ShieldedSerialPub, voucher.m_Ticket.m_SerialPub);

            // save Kernel and KernelID
            pKrnOut->MsgToID();

            if (!bSendingToSelf)
            {
                TxKernelStd::Ptr pKrnWrap = std::make_unique<TxKernelStd>();
                pKrnWrap->m_Height = hr;
                pKrnWrap->m_vNested.push_back(std::move(pKrnOut));
                pKrnWrap->Sign(kWrap);

                pKrn = std::move(pKrnWrap);
            }
            else
                pKrn = std::move(pKrnOut);


            m_Tx.SetParameter(TxParameterID::KernelID, pKrn->m_Internal.m_ID);
            m_Tx.SetParameter(TxParameterID::Kernel, pKrn);

            vouchers.pop_back();
            m_Tx.SetParameter(TxParameterID::UnusedShieldedVoucherList, vouchers);
        }

        LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
            << " Transaction created. Kernel: " << GetKernelIDString()
            << ", min height: " << pKrn->m_Height.m_Min
            << ", max height: " << pKrn->m_Height.m_Max;

        transaction->m_vKernels.push_back(std::move(pKrn));

        offset -= pars.m_Output.m_k;

        transaction->m_Offset = offset;
        transaction->Normalize();

        return transaction;
    }
} // namespace beam::wallet::lelantus