// Copyright 2018 The Beam Team
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

#include "wallet_transaction.h"
#include "base_tx_builder.h"
#include "core/block_crypt.h"

#include <numeric>
#include "utility/logger.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;


    SimpleTransaction::SimpleTransaction(INegotiatorGateway& gateway
                                        , beam::IWalletDB::Ptr walletDB
                                        , const TxID& txID)
        : BaseTransaction{ gateway, walletDB, txID }
    {

    }

    TxType SimpleTransaction::GetType() const
    {
        return TxType::Simple;
    }

    void SimpleTransaction::UpdateImpl()
    {
        bool isSender = GetMandatoryParameter<bool>(TxParameterID::IsSender);
        bool isSelfTx = IsSelfTx();
        State txState = GetState();

        AmountList amoutList;
        if (!GetParameter(TxParameterID::AmountList, amoutList))
        {
            amoutList = AmountList{ GetMandatoryParameter<Amount>(TxParameterID::Amount) };
        }

        BaseTxBuilder builder{ *this, kDefaultSubTxID, amoutList, GetMandatoryParameter<Amount>(TxParameterID::Fee) };
        if (!builder.GetInitialTxParams() && txState == State::Initial)
        {
            LOG_INFO() << GetTxID() << (isSender ? " Sending " : " Receiving ") << PrintableAmount(builder.GetAmount()) << " (fee: " << PrintableAmount(builder.GetFee()) << ")";

            if (CheckExpired())
            {
                return;
            }

            if (isSender)
            {
                builder.SelectInputs();
                builder.AddChangeOutput();
            }

            if (isSelfTx || !isSender)
            {
                // create receiver utxo
                for (auto& amount : builder.GetAmountList())
                {
                    builder.AddOutput(amount, false);
                }
            }

            if (!builder.FinalizeOutputs())
            {
                // TODO: transaction is too big :(
            }

            UpdateTxDescription(TxStatus::InProgress);
        }

        uint64_t nAddrOwnID;
        if (!GetParameter(TxParameterID::MyAddressID, nAddrOwnID))
        {
            WalletID wid;
            if (GetParameter(TxParameterID::MyID, wid))
            {
                auto waddr = m_WalletDB->getAddress(wid);
                if (waddr && waddr->m_OwnID)
                    SetParameter(TxParameterID::MyAddressID, waddr->m_OwnID);
            }
        }

        builder.CreateKernel();
        
        if (!isSelfTx && !builder.GetPeerPublicExcessAndNonce())
        {
            assert(IsInitiator());
            if (txState == State::Initial)
            {
                SendInvitation(builder, isSender);
                SetState(State::Invitation);
            }
            return;
        }

        builder.SignPartial();

        bool hasPeersInputsAndOutputs = builder.GetPeerInputsAndOutputs();
        if (!isSelfTx && !builder.GetPeerSignature())
        {
            if (txState == State::Initial)
            {
                // invited participant
                assert(!IsInitiator());
                
                UpdateTxDescription(TxStatus::Registering);
                ConfirmInvitation(builder, !hasPeersInputsAndOutputs);

                uint32_t nVer = 0;
                if (GetParameter(TxParameterID::PeerProtoVersion, nVer))
                {
                    // for peers with new flow, we assume that after we have responded, we have to switch to the state of awaiting for proofs
                    SetParameter(TxParameterID::TransactionRegistered, true);

                    SetState(State::KernelConfirmation);
                    ConfirmKernel(builder.GetKernel());
                }
                else
                {
                    SetState(State::InvitationConfirmation);
                }
                return;
            }
            if (IsInitiator())
            {
                return;
            }
        }

        if (IsInitiator() && !builder.IsPeerSignatureValid())
        {
            OnFailed(TxFailureReason::InvalidPeerSignature, true);
            return;
        }

        if (!isSelfTx && isSender && IsInitiator())
        {
            // verify peer payment acknowledgement

            wallet::PaymentConfirmation pc;
            WalletID widPeer, widMy;
            bool bSuccess =
                GetParameter(TxParameterID::PeerID, widPeer) &&
                GetParameter(TxParameterID::MyID, widMy) &&
                GetParameter(TxParameterID::KernelID, pc.m_KernelID) &&
                GetParameter(TxParameterID::Amount, pc.m_Value) &&
                GetParameter(TxParameterID::PaymentConfirmation, pc.m_Signature);

            if (bSuccess)
            {
                pc.m_Sender = widMy.m_Pk;
                bSuccess = pc.IsValid(widPeer.m_Pk);
            }

            if (!bSuccess)
            {
                if (!get_PeerVersion())
                {
                    // older wallets don't support it. Check if unsigned payments are ok
                    uint8_t nRequired = 0;
                    wallet::getVar(*m_WalletDB, wallet::g_szPaymentProofRequired, nRequired);

                    if (!nRequired)
                        bSuccess = true;
                }

                if (!bSuccess)
                {
                    OnFailed(TxFailureReason::NoPaymentProof);
                    return;
                }
            }

        }

        builder.FinalizeSignature();

        bool isRegistered = false;
        if (!GetParameter(TxParameterID::TransactionRegistered, isRegistered))
        {
            if (!isSelfTx && (!hasPeersInputsAndOutputs || IsInitiator()))
            {
                if (txState == State::Invitation)
                {
                    UpdateTxDescription(TxStatus::Registering);
                    ConfirmTransaction(builder, !hasPeersInputsAndOutputs);
                    SetState(State::PeerConfirmation);
                }
                if (!hasPeersInputsAndOutputs)
                {
                    return;
                }
            }

            // Construct transaction
            auto transaction = builder.CreateTransaction();

            // Verify final transaction
            TxBase::Context ctx;
            if (!transaction->IsValid(ctx))
            {
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return;
            }
            m_Gateway.register_tx(GetTxID(), transaction);
            SetState(State::Registration);
            return;
        }

        if (!isRegistered)
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
            return;
        }

        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            if (txState == State::Registration)
            {
                uint32_t nVer = 0;
                if (!GetParameter(TxParameterID::PeerProtoVersion, nVer))
                {
                    // notify old peer that transaction has been registered
                    NotifyTransactionRegistered();
                }
            }
            SetState(State::KernelConfirmation);
            ConfirmKernel(builder.GetKernel());
            return;
        }

        vector<Coin> modified;
        m_WalletDB->visit([&](const Coin& coin)
        {
            bool bIn = (coin.m_createTxId == m_ID);
            bool bOut = (coin.m_spentTxId == m_ID);
            if (bIn || bOut)
            {
                modified.emplace_back();
                Coin& c = modified.back();
                c = coin;

                if (bIn)
                {
                    c.m_confirmHeight = std::min(c.m_confirmHeight, hProof);
                    c.m_maturity = hProof + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
                }
                if (bOut)
                    c.m_spentHeight = std::min(c.m_spentHeight, hProof);
            }

            return true;
        });


        GetWalletDB()->save(modified);

        CompleteTx();
    }

    void SimpleTransaction::SendInvitation(const BaseTxBuilder& builder, bool isSender)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::Amount, builder.GetAmount())
            .AddParameter(TxParameterID::Fee, builder.GetFee())
            .AddParameter(TxParameterID::MinHeight, builder.GetMinHeight())
            .AddParameter(TxParameterID::MaxHeight, builder.GetMaxHeight())
            .AddParameter(TxParameterID::IsSender, !isSender)
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce());

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void SimpleTransaction::ConfirmInvitation(const BaseTxBuilder& builder, bool sendUtxos)
    {
        LOG_INFO() << GetTxID() << " Transaction accepted. Kernel: " << builder.GetKernelIDString();
        SetTxParameter msg;
        msg
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerSignature, builder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce());
        if (sendUtxos)
        {
            msg.AddParameter(TxParameterID::PeerInputs, builder.GetInputs())
            .AddParameter(TxParameterID::PeerOutputs, builder.GetOutputs())
            .AddParameter(TxParameterID::PeerOffset, builder.GetOffset());
        }

        assert(!IsSelfTx());
        if (!GetMandatoryParameter<bool>(TxParameterID::IsSender))
        {
            wallet::PaymentConfirmation pc;
            WalletID widPeer, widMy;
            bool bSuccess =
                GetParameter(TxParameterID::PeerID, widPeer) &&
                GetParameter(TxParameterID::MyID, widMy) &&
                GetParameter(TxParameterID::KernelID, pc.m_KernelID) &&
                GetParameter(TxParameterID::Amount, pc.m_Value);

            if (bSuccess)
            {
                pc.m_Sender = widPeer.m_Pk;

                auto waddr = m_WalletDB->getAddress(widMy);
                if (waddr && waddr->m_OwnID)
                {
                    Scalar::Native sk;
                    m_WalletDB->get_MasterKdf()->DeriveKey(sk, Key::ID(waddr->m_OwnID, Key::Type::Bbs));

                    proto::Sk2Pk(widMy.m_Pk, sk);

                    pc.Sign(sk);
                    msg.AddParameter(TxParameterID::PaymentConfirmation, pc.m_Signature);
                }
            }
        }

        SendTxParameters(move(msg));
    }

    void SimpleTransaction::ConfirmTransaction(const BaseTxBuilder& builder, bool sendUtxos)
    {
        uint32_t nVer = 0;
        if (GetParameter(TxParameterID::PeerProtoVersion, nVer))
        {
            // we skip this step for new tx flow
            return;
        }
        LOG_INFO() << GetTxID() << " Peer signature is valid. Kernel: " << builder.GetKernelIDString();
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::PeerSignature, Scalar(builder.GetPartialSignature()));
        if (sendUtxos)
        {
            msg.AddParameter(TxParameterID::PeerInputs, builder.GetInputs())
                .AddParameter(TxParameterID::PeerOutputs, builder.GetOutputs())
                .AddParameter(TxParameterID::PeerOffset, builder.GetOffset());
        }
        SendTxParameters(move(msg));
    }

    void SimpleTransaction::NotifyTransactionRegistered()
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::TransactionRegistered, true);
        SendTxParameters(move(msg));
    }

    bool SimpleTransaction::IsSelfTx() const
    {
        WalletID peerID = GetMandatoryParameter<WalletID>(TxParameterID::PeerID);
        auto address = m_WalletDB->getAddress(peerID);
        return address.is_initialized() && address->m_OwnID;
    }

    SimpleTransaction::State SimpleTransaction::GetState() const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state);
        return state;
    }

    bool SimpleTransaction::ShouldNotifyAboutChanges(TxParameterID paramID) const
    {
        switch (paramID)
        {
        case TxParameterID::Amount:
        case TxParameterID::Fee:
        case TxParameterID::MinHeight:
        case TxParameterID::PeerID:
        case TxParameterID::MyID:
        case TxParameterID::CreateTime:
        case TxParameterID::IsSender:
        case TxParameterID::Status:
        case TxParameterID::TransactionType:
        case TxParameterID::KernelID:
            return true;
        default:
            return false;
        }
    }
}
