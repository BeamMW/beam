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
            UpdateTxDescription(TxStatus::InProgress);

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
            //auto transaction = m_TxBuilder->CreateTransaction();
            auto transaction = std::make_shared<Transaction>();

            {               
                transaction->m_vInputs = GetMandatoryParameter<std::vector<Input::Ptr>>(TxParameterID::Inputs);

                {
                    std::vector<Output::Ptr> outputs;
                    if (GetParameter(TxParameterID::Outputs, outputs))
                    {
                        transaction->m_vOutputs = std::move(outputs);
                    }
                }
                //transaction->m_Offset = m_Offset;
                {
                    ECC::Scalar::Native offset = Zero;

                    for (auto id : m_TxBuilder->GetInputCoins())
                    {
                        ECC::Scalar::Native k;
                        ECC::Point comm;
                        CoinID::Worker(id).Create(k, comm, *GetWalletDB()->get_MasterKdf());

                        offset += k;
                    }

                    transaction->m_Offset = offset;
                }

                {
                    ECC::Scalar::Native offset = transaction->m_Offset;

                    for (auto id : m_TxBuilder->GetOutputCoins())
                    {
                        ECC::Scalar::Native k;
                        ECC::Point comm;
                        CoinID::Worker(id).Create(k, comm, *GetWalletDB()->get_MasterKdf());

                        offset -= k;
                    }

                    transaction->m_Offset = offset;
                }

                {
                    TxKernelShieldedOutput::Ptr pKrn(new TxKernelShieldedOutput);
                    pKrn->m_Height.m_Min = GetWalletDB()->getCurrentHeight();
                    pKrn->m_Fee = m_TxBuilder->GetFee();

                    ShieldedTxo::Viewer viewer;
                    viewer.FromOwner(*GetWalletDB()->get_MasterKdf());

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
                    op.m_Sender = GetMandatoryParameter<WalletID>(TxParameterID::MyID).m_Pk;
                    //op.m_Message = m_Shielded.m_Message;
                    op.m_Value = m_TxBuilder->GetAmount();
                    op.Generate(pKrn->m_Txo, oracle, viewer, outputNonce);

                    // save shielded Coin
                    ShieldedCoin shieldedCoin;
                    shieldedCoin.m_value = m_TxBuilder->GetAmount();
                    shieldedCoin.m_createTxId = GetTxID();
                    shieldedCoin.m_skSerialG = sp.m_pK[0];
                    shieldedCoin.m_skOutputG = op.m_k;
                    shieldedCoin.m_serialPub = pKrn->m_Txo.m_Serial.m_SerialPub;
                    shieldedCoin.m_IsCreatedByViewer = sp.m_IsCreatedByViewer;

                    SetParameter(TxParameterID::ShieldedCoin, shieldedCoin);

                    // save KernelID
                    pKrn->MsgToID();
                    SetParameter(TxParameterID::KernelID, pKrn->m_Internal.m_ID);

                    // verify TxKernelShieldedOutput
                    ECC::Point::Native pt;
                    assert(pKrn->IsValid(GetWalletDB()->getCurrentHeight(), pt));

                    transaction->m_vKernels.push_back(std::move(pKrn));

                    ECC::Scalar::Native offset = transaction->m_Offset;
                    offset -= op.m_k;
                    transaction->m_Offset = offset;
                }
                transaction->Normalize();
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

        // getProofShieldedOutp
        if (m_waitingShieldedProof)
        {
            auto shieldedCoin = GetMandatoryParameter<ShieldedCoin>(TxParameterID::ShieldedCoin);

            GetGateway().get_proof_shielded_output(GetTxID(), shieldedCoin.m_serialPub, [this](proto::ProofShieldedOutp proof)
                {
                    if (m_waitingShieldedProof)
                    {
                        m_waitingShieldedProof = false;

                        // update shielded output
                        auto coin = GetMandatoryParameter<ShieldedCoin>(TxParameterID::ShieldedCoin);
                        coin.m_ID = proof.m_ID;

                        // save shielded output to DB
                        GetWalletDB()->saveShieldedCoin(coin);
                    }
                    UpdateAsync();
                });
            return;
        }

        SetCompletedTxCoinStatuses(hProof);

        CompleteTx();
    }
} // namespace beam::wallet::lelantus