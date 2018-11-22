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

#include "swap_transaction.h"

using namespace std;
using namespace ECC;

namespace beam {namespace wallet
{
    AtomicSwapTransaction::AtomicSwapTransaction(INegotiatorGateway& gateway
                                               , beam::IWalletDB::Ptr walletDB
                                               , const TxID& txID)
        : BaseTransaction(gateway, walletDB, txID)
    {

    }

    TxType AtomicSwapTransaction::GetType() const
    {
        return TxType::AtomicSwap;
    }

    void AtomicSwapTransaction::UpdateImpl()
    {
        /*bool sender = false;
        GetMandatoryParameter(TxParameterID::IsSender, sender);
        if (sender)
        {
            Send();
        }
        else
        {
            Receive();
        }*/
    }

    //void AtomicSwapTransaction::Send()
    //{
    //    Amount amount = 0, fee = 0;
    //    GetMandatoryParameter(TxParameterID::Amount, amount);
    //    GetMandatoryParameter(TxParameterID::Fee, fee);

    //    TxBuilder sharedSb{ *this };
    //    Height minHeight = 0;
    //    
    //    vector<Input::Ptr> sharedInputs;
    //    vector<Output::Ptr> sharedOutputs;

    //    Scalar::Native sharedOffset;
    //    Scalar::Native sharedBlindingExcess;
    //    Scalar::Native sharedBlindingFactor;

    //    if (!GetParameter(TxParameterID::SharedBlindingExcess, sharedBlindingExcess)
    //        || !GetParameter(TxParameterID::SharedOffset, sharedOffset)
    //        || !GetParameter(TxParameterID::MinHeight, minHeight)
    //        || !GetParameter(TxParameterID::SharedBlindingFactor, sharedBlindingFactor)
    //        || !GetParameter(TxParameterID::SharedInputs, sharedInputs)
    //        || !GetParameter(TxParameterID::SharedOutputs, sharedOutputs))
    //    {
    //        LOG_INFO() << GetTxID() << (" Sending ") << PrintableAmount(amount) << " (fee: " << PrintableAmount(fee) << ")";
    //        minHeight = m_WalletDB->getCurrentHeight();
    //        SetParameter(TxParameterID::MinHeight, minHeight);

    //        // select and lock input utxos
    //        {
    //            Amount amountWithFee = amount + fee;
    //            PrepareSenderUTXOs(amountWithFee, minHeight);
    //            SetParameter(TxParameterID::SharedPeerInputs, GetTxInputs(GetTxID()));
    //        }

    //        // create shared output.
    //        {
    //            Coin newUtxo{ amount, Coin::Draft, minHeight };
    //            newUtxo.m_createTxId = GetTxID();
    //            m_WalletDB->store(newUtxo);

    //            Scalar::Native blindingFactor = m_WalletDB->calcKey(newUtxo);
    //            auto[privateFactor, newOffset] = splitKey(blindingFactor, newUtxo.m_id);

    //            blindingFactor = -privateFactor;
    //            sharedBlindingExcess += blindingFactor;
    //            sharedOffset += newOffset;

    //            SetParameter(TxParameterID::SharedBlindingExcess, sharedBlindingExcess);
    //            SetParameter(TxParameterID::SharedOffset, sharedOffset);
    //            SetParameter(TxParameterID::SharedBlindingFactor, blindingFactor);
    //            sharedBlindingFactor = blindingFactor;
    //        }

    //        UpdateTxDescription(TxStatus::InProgress);
    //    }

    //    sharedSb.CreateKernel(fee, minHeight);
    //    sharedSb.ApplyBlindingExcess(TxParameterID::SharedBlindingExcess);

    //    Point::Native publicSharedBlindingFactor = Context::get().G * sharedBlindingFactor;

    //    Point::Native peerPublicSharedBlindingFactor;
    //    if (!GetParameter(TxParameterID::PeerPublicSharedBlindingFactor, peerPublicSharedBlindingFactor))
    //    {
    //        assert(IsInitiator());

    //        SetTxParameter msg;
    //        msg.AddParameter(TxParameterID::Amount, amount)
    //            .AddParameter(TxParameterID::Fee, fee)
    //            .AddParameter(TxParameterID::MinHeight, minHeight)
    //            .AddParameter(TxParameterID::IsSender, false)
    //            .AddParameter(TxParameterID::PeerPublicSharedBlindingFactor, publicSharedBlindingFactor);

    //        SendTxParameters(move(msg));
    //        return;
    //    }

    //    // create locked rollback tx
    //    {
    //        TxBuilder lockedSb(*this);
    //        Height lockHeight = minHeight + 1440; // 24 hours
    //        Scalar::Native lockedOffset;
    //        Scalar::Native lockedBlindingExcess;
    //        Scalar::Native lockedBlindingFactor;
    //        if (!GetParameter(TxParameterID::LockedBlindingExcess, lockedBlindingExcess)
    //            || !GetParameter(TxParameterID::LockedOffset, lockedOffset)
    //            || !GetParameter(TxParameterID::LockedBlindingFactor, lockedBlindingFactor))
    //        {
    //            // output for locked rollback utxo, it is mine
    //            {
    //                Coin newUtxo{ amount, Coin::Draft, minHeight };
    //                newUtxo.m_createTxId = GetTxID();
    //                m_WalletDB->store(newUtxo);

    //                Scalar::Native blindingFactor = m_WalletDB->calcKey(newUtxo);
    //                auto[privateFactor, newOffset] = splitKey(blindingFactor, newUtxo.m_id);

    //                blindingFactor = -privateFactor;
    //                lockedBlindingExcess += blindingFactor;
    //                lockedOffset += newOffset;

    //                SetParameter(TxParameterID::LockedBlindingExcess, lockedBlindingExcess);
    //                SetParameter(TxParameterID::LockedOffset, lockedOffset);
    //                SetParameter(TxParameterID::LockedBlindingFactor, blindingFactor);
    //                lockedBlindingFactor = blindingFactor;
    //            }
    //        }

    //        Point::Native sharedCommitment = Commitment(sharedBlindingFactor, amount);
    //        sharedCommitment += peerPublicSharedBlindingFactor;

    //        Input::Ptr sharedInput = make_unique<Input>();
    //        sharedInput->m_Commitment = sharedCommitment;

    //        lockedSb.CreateKernel(0, lockHeight);

    //        lockedSb.ApplyBlindingExcess(TxParameterID::LockedBlindingExcess);

    //        Output::Ptr lockedOutput = make_unique<Output>();
    //        lockedOutput->m_Coinbase = false;
    //        lockedOutput->Create(lockedBlindingFactor, amount);

    //        if (!lockedSb.ApplyPublicPeerNonce(TxParameterID::LockedPeerPublicNonce)
    //            || !lockedSb.ApplyPublicPeerExcess(TxParameterID::LockedPeerPublicExcess))
    //        {
    //            // invite
    //            assert(IsInitiator());
    //            vector<Input::Ptr> inputs;
    //            inputs.push_back(move(sharedInput));
    //            vector<Output::Ptr> outputs;
    //            outputs.push_back(move(lockedOutput));
    //            SetTxParameter msg;
    //            msg.AddParameter(TxParameterID::LockedAmount, amount)
    //               //.AddParameter(TxParameterID::Fee, fee)
    //               .AddParameter(TxParameterID::LockedMinHeight, lockHeight)
    //               .AddParameter(TxParameterID::IsSender, false)
    //               .AddParameter(TxParameterID::LockedPeerInputs, move(inputs))
    //               .AddParameter(TxParameterID::LockedPeerOutputs, move(outputs))
    //               .AddParameter(TxParameterID::LockedPeerPublicExcess, lockedSb.m_PublicExcess)
    //               .AddParameter(TxParameterID::LockedPeerPublicNonce, lockedSb.m_PublicNonce)
    //               .AddParameter(TxParameterID::LockedPeerOffset, lockedOffset);
    //            SendTxParameters(move(msg));
    //            return;
    //        }

    //        lockedSb.SignPartial();
    //        if (!lockedSb.ApplyPeerSignature(TxParameterID::LockedPeerSignature)
    //            || !lockedSb.IsPeerSignatureValid())
    //        {
    //            OnFailed(true);
    //            return;
    //        }

    //        lockedSb.FinalizeSignature();
    //        // here we have everithing to create rollback transaction
    //    }


    //    if (!sharedSb.ApplyPublicPeerNonce(TxParameterID::SharedPeerPublicNonce)
    //        || !sharedSb.ApplyPublicPeerExcess(TxParameterID::SharedPeerPublicExcess)
    //        || !sharedSb.ApplyPeerSignature(TxParameterID::SharedPeerSignature))
    //    {
    //        SetTxParameter msg;
    //        msg.AddParameter(TxParameterID::Amount, amount)
    //           .AddParameter(TxParameterID::Fee, fee)
    //           .AddParameter(TxParameterID::MinHeight, minHeight)
    //           .AddParameter(TxParameterID::IsSender, false)
    //           .AddParameter(TxParameterID::SharedPeerInputs, GetTxInputs(GetTxID()))
    //           .AddParameter(TxParameterID::SharedPeerOutputs, GetTxOutputs(GetTxID()))
    //           .AddParameter(TxParameterID::SharedPeerPublicExcess, sharedSb.m_PublicExcess)
    //           .AddParameter(TxParameterID::SharedPeerPublicNonce, sharedSb.m_PublicNonce)
    //           .AddParameter(TxParameterID::SharedPeerOffset, sharedOffset);
    //        SendTxParameters(move(msg));
    //        return;
    //    }

    //    sharedSb.SignPartial();

    //    // verify peer's signature
    //    if (!sharedSb.IsPeerSignatureValid())
    //    {
    //        OnFailed(true);
    //        return;
    //    }

    //    sharedSb.FinalizeSignature();

    //    CompleteTx();
    //}

    //void AtomicSwapTransaction::Receive()
    //{
    //    Amount amount = 0, fee = 0;
    //    GetMandatoryParameter(TxParameterID::Amount, amount);
    //    GetMandatoryParameter(TxParameterID::Fee, fee);

    //    TxBuilder sharedSb{ *this };
    //    Height minHeight = 0;
    //    Scalar::Native sharedOffset;
    //    Scalar::Native sharedBlindingExcess;
    //    Scalar::Native sharedBlindingFactor;

    //    if (!GetParameter(TxParameterID::SharedBlindingExcess, sharedBlindingExcess)
    //        || !GetParameter(TxParameterID::SharedOffset, sharedOffset)
    //        || !GetParameter(TxParameterID::MinHeight, minHeight)
    //        || !GetParameter(TxParameterID::SharedBlindingFactor, sharedBlindingFactor))
    //    {
    //        LOG_INFO() << GetTxID() << (" Receiving ") << PrintableAmount(amount) << " (fee: " << PrintableAmount(fee) << ")";
    //        minHeight = m_WalletDB->getCurrentHeight();
    //        SetParameter(TxParameterID::MinHeight, minHeight);

    //        // create shared output. the same for sender and receiver
    //        {
    //            Coin newUtxo{ amount, Coin::Draft, minHeight };
    //            newUtxo.m_createTxId = GetTxID();
    //            m_WalletDB->store(newUtxo);

    //            Scalar::Native blindingFactor = m_WalletDB->calcKey(newUtxo);
    //            auto[privateFactor, newOffset] = splitKey(blindingFactor, newUtxo.m_id);

    //            blindingFactor = -privateFactor;
    //            sharedBlindingExcess += blindingFactor;
    //            sharedOffset += newOffset;

    //            SetParameter(TxParameterID::SharedBlindingExcess, sharedBlindingExcess);
    //            SetParameter(TxParameterID::SharedOffset, sharedOffset);
    //            SetParameter(TxParameterID::SharedBlindingFactor, blindingFactor);
    //            sharedBlindingFactor = blindingFactor;
    //        }

    //        UpdateTxDescription(TxStatus::InProgress);
    //    }

    //    sharedSb.CreateKernel(fee, minHeight);
    //    sharedSb.ApplyBlindingExcess(TxParameterID::SharedBlindingExcess);


    //    TxBuilder lockedSb(*this);

    //    Point::Native publicSharedBlindingFactor = Context::get().G * sharedBlindingFactor;

    //    Point::Native peerSharedBlindingFactor;
    //    if (!lockedSb.ApplyPublicPeerNonce(TxParameterID::LockedPeerPublicNonce)
    //        || !lockedSb.ApplyPublicPeerExcess(TxParameterID::LockedPeerPublicExcess))
    //    {
    //        // accept invitation 
    //        assert(!IsInitiator());
    //        SetTxParameter msg;
    //        msg.AddParameter(TxParameterID::PeerPublicSharedBlindingFactor, publicSharedBlindingFactor);
    //        SendTxParameters(move(msg));
    //        return;
    //    }

    //    // create locked rollback tx
    //    {
    //        Height lockHeight = minHeight + 1440; // 24 hours
    //        lockedSb.CreateKernel(0, lockHeight);

    //        SetParameter(TxParameterID::LockedBlindingExcess, Scalar::Native());
    //        lockedSb.ApplyBlindingExcess(TxParameterID::LockedBlindingExcess);

    //        lockedSb.SignPartial();

    //        if (!sharedSb.ApplyPublicPeerNonce(TxParameterID::SharedPeerPublicNonce)
    //            || !sharedSb.ApplyPublicPeerExcess(TxParameterID::SharedPeerPublicExcess))
    //        {
    //            // Confirm invitation
    //            SetTxParameter msg;
    //            msg.AddParameter(TxParameterID::LockedPeerPublicExcess, lockedSb.m_PublicExcess)
    //               .AddParameter(TxParameterID::LockedPeerSignature, lockedSb.m_PartialSignature)
    //               .AddParameter(TxParameterID::LockedPeerPublicNonce, lockedSb.m_PublicNonce);

    //            SendTxParameters(move(msg));
    //            return;
    //        }
    //    }

    //    sharedSb.SignPartial();

    //    if (!sharedSb.ApplyPeerSignature(TxParameterID::SharedPeerSignature))
    //    {
    //        // invited participant
    //        assert(!IsInitiator());
    //        // Confirm invitation
    //        SetTxParameter msg;
    //        msg.AddParameter(TxParameterID::SharedPeerPublicExcess, sharedSb.m_PublicExcess)
    //           .AddParameter(TxParameterID::SharedPeerSignature, sharedSb.m_PartialSignature)
    //           .AddParameter(TxParameterID::SharedPeerPublicNonce, sharedSb.m_PublicNonce);

    //        SendTxParameters(move(msg));
    //        return;
    //    }

    //    // verify peer's signature
    //    if (!sharedSb.IsPeerSignatureValid())
    //    {
    //        OnFailed(true);
    //        return;
    //    }

    //    sharedSb.FinalizeSignature();

    //    CompleteTx();
    //}

}} // namespace