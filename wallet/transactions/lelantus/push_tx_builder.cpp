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

        ShieldedTxo::Data::OutputParams op;
        op.m_Value = GetAmount();
        op.m_AssetID = GetAssetId();
        ZeroObject(op.m_User);

        op.m_User.m_Sender = m_Tx.GetMandatoryParameter<WalletID>(TxParameterID::MyID).m_Pk;
        // TODO: add ShieldedMessage if needed
        // op.m_User.m_Message = m_Tx.GetMandatoryParameter<WalletID>(TxParameterID::ShieldedMessage);

        ShieldedVoucherList vouchers;
        if (!m_Tx.GetParameter(TxParameterID::UnusedShieldedVoucherList, vouchers))
        {
            if (!m_Tx.GetParameter(TxParameterID::ShieldedVoucherList, vouchers))
            {
                // no voucher - means we're sending to ourselves. Create our voucher
                ShieldedTxo::Voucher& voucher = vouchers.emplace_back();
                ShieldedTxo::Viewer viewer;
                const Key::Index nIdx = 0;
                viewer.FromOwner(*m_Tx.GetWalletDB()->get_OwnerKdf(), nIdx);

                ECC::GenRandom(voucher.m_SharedSecret); // not yet, just a nonce placeholder

                ShieldedTxo::Data::TicketParams tp;
                tp.Generate(voucher.m_Ticket, viewer, voucher.m_SharedSecret);

                voucher.m_SharedSecret = tp.m_SharedSecret;
                ZeroObject(voucher.m_Signature);

                // save shielded Coin
                ShieldedCoin shieldedCoin;
                shieldedCoin.m_value = op.m_Value;
                shieldedCoin.m_assetID = op.m_AssetID;
                shieldedCoin.m_createTxId = m_Tx.GetTxID();
                shieldedCoin.m_Key.m_kSerG = tp.m_pK[0];
                shieldedCoin.m_Key.m_IsCreatedByViewer = tp.m_IsCreatedByViewer;
                shieldedCoin.m_Key.m_nIdx = nIdx;
                shieldedCoin.m_User = op.m_User;

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
        op.Restore_kG(voucher.m_SharedSecret);

        TxKernelShieldedOutput::Ptr pKrn;
        if (!m_Tx.GetParameter(TxParameterID::Kernel, pKrn) || !pKrn)
        {
            pKrn = std::make_unique<TxKernelShieldedOutput>();
            pKrn->m_Height.m_Min = GetMinHeight();
            pKrn->m_Height.m_Max = GetMaxHeight();
            pKrn->m_Fee = GetFee();

            pKrn->UpdateMsg();
            ECC::Oracle oracle;
            oracle << pKrn->m_Msg;

            pKrn->m_Txo.m_Ticket = voucher.m_Ticket;

            if (IsAssetTx())
            {
                pKrn->m_Txo.m_pAsset = std::make_unique<Asset::Proof>();
                //pKrn->m_Txo.m_pAsset->Create()
			    //m_pAsset->Create(wrk.m_hGen, skSign, cid.m_Value, cid.m_AssetID, wrk.m_hGen, bUseCoinKdf ? nullptr : &hv);
                //pKrn->m_Txo.m_pAsset =
            }

            op.Generate(pKrn->m_Txo, voucher.m_SharedSecret, oracle);

            m_Tx.SetParameter(TxParameterID::ShieldedSerialPub, voucher.m_Ticket.m_SerialPub);

            // save Kernel and KernelID
            pKrn->MsgToID();
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

        offset -= op.m_k;

        transaction->m_Offset = offset;
        transaction->Normalize();

        return transaction;
    }
} // namespace beam::wallet::lelantus