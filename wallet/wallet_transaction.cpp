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
#include "wallet.h"
#include "core/block_crypt.h"

#include <numeric>
#include "utility/logger.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    TxParameters CreateSimpleTransactionParameters(boost::optional<TxID> txId)
    {
        return CreateTransactionParameters(TxType::Simple, txId ? *txId : GenerateTxID()).SetParameter(TxParameterID::TransactionType, TxType::Simple);
    }

    TxParameters CreateSplitTransactionParameters(const WalletID& myID, const AmountList& amountList, boost::optional<TxID> txId)
    {
        return CreateSimpleTransactionParameters(txId)
            .SetParameter(TxParameterID::MyID, myID)
            .SetParameter(TxParameterID::PeerID, myID)
            .SetParameter(TxParameterID::AmountList, amountList)
            .SetParameter(TxParameterID::Amount, std::accumulate(amountList.begin(), amountList.end(), Amount(0)));
    }

    SimpleTransaction::Creator::Creator(IWalletDB::Ptr walletDB)
        : m_WalletDB(walletDB)
    {

    }

    BaseTransaction::Ptr SimpleTransaction::Creator::Create(INegotiatorGateway& gateway
                                                          , IWalletDB::Ptr walletDB
                                                          , IPrivateKeyKeeper::Ptr keyKeeper
                                                          , const TxID& txID)
    {
        return BaseTransaction::Ptr(new SimpleTransaction(gateway, walletDB, keyKeeper, txID));
    }

    TxParameters SimpleTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        auto peerID = parameters.GetParameter<WalletID>(TxParameterID::PeerID);
        if (!peerID)
        {
            throw InvalidTransactionParametersException();
        }
        auto receiverAddr = m_WalletDB->getAddress(*peerID);
        if (receiverAddr)
        {
            if (receiverAddr->m_OwnID && receiverAddr->isExpired())
            {
                LOG_INFO() << "Can't send to the expired address.";
                throw AddressExpiredException();
            }
            TxParameters temp{ parameters };
            temp.SetParameter(TxParameterID::IsSelfTx, receiverAddr->m_OwnID != 0);
            return temp;
        }
        else
        {
            WalletAddress address;
            address.m_walletID = *peerID;
            address.m_createTime = getTimestamp();
            if (auto message = parameters.GetParameter(TxParameterID::Message); message)
            {
                address.m_label = std::string(message->begin(), message->end());
            }

            m_WalletDB->saveAddress(address);
        }
        return parameters;
    }

    SimpleTransaction::SimpleTransaction(INegotiatorGateway& gateway
                                       , IWalletDB::Ptr walletDB
                                       , IPrivateKeyKeeper::Ptr keyKeeper
                                       , const TxID& txID)
        : BaseTransaction{ gateway, walletDB, keyKeeper, txID }
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

        if (!m_TxBuilder)
        {
            m_TxBuilder = make_shared<BaseTxBuilder>(*this, kDefaultSubTxID, amoutList, GetMandatoryParameter<Amount>(TxParameterID::Fee));
        }
        auto sharedBuilder = m_TxBuilder;
        BaseTxBuilder& builder = *sharedBuilder;

        bool hasPeersInputsAndOutputs = builder.GetPeerInputsAndOutputs();

        // Check if we already have signed kernel
        if ((isSender && !builder.LoadKernel())
         || (!isSender && !builder.HasKernelID()))
        {
            // We don't need key keeper initialized to go on beyond this point
            if (!m_KeyKeeper)
            {
                // public wallet
                return;
            }

            if (!builder.GetInitialTxParams() && txState == State::Initial)
            {
                LOG_INFO() << GetTxID() << (isSender ? " Sending " : " Receiving ")
                    << PrintableAmount(builder.GetAmount())
                    << " (fee: " << PrintableAmount(builder.GetFee()) << ")";

                if (isSender)
                {
                    Height maxResponseHeight = 0;
                    if (GetParameter(TxParameterID::PeerResponseHeight, maxResponseHeight))
                    {
                        LOG_INFO() << GetTxID() << " Max height for response: " << maxResponseHeight;
                    }

                    builder.SelectInputs();
                    builder.AddChange();
                }

                if (isSelfTx || !isSender)
                {
                    // create receiver utxo
                    for (const auto& amount : builder.GetAmountList())
                    {
                        builder.GenerateNewCoin(amount, false);
                    }
                }

                UpdateTxDescription(TxStatus::InProgress);

                builder.GenerateOffset();
            }

            if (builder.CreateInputs())
            {
                return;
            }

            if (builder.CreateOutputs())
            {
                return;
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

            builder.GenerateNonce();

            if (!isSelfTx && !builder.GetPeerPublicExcessAndNonce())
            {
                assert(IsInitiator());
                if (txState == State::Initial)
                {
                    SendInvitation(builder, isSender);
                    SetState(State::Invitation);
                }
                UpdateOnNextTip();
                return;
            }

            if (!builder.UpdateMaxHeight())
            {
                OnFailed(TxFailureReason::MaxHeightIsUnacceptable, true);
                return;
            }

            builder.CreateKernel();
            builder.SignPartial();

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
						uint8_t nCode = proto::TxStatus::Ok; // compiler workaround (ref to static const)
						SetParameter(TxParameterID::TransactionRegistered, nCode);

                        SetState(State::KernelConfirmation);
                        ConfirmKernel(builder.GetKernelID());
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
                // verify peer payment confirmation

                PaymentConfirmation pc;
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
                        storage::getVar(*m_WalletDB, storage::g_szPaymentProofRequired, nRequired);

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
        }

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
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

            if (CheckExpired())
            {
                return;
            }

            // Construct transaction
            auto transaction = builder.CreateTransaction();

            // Verify final transaction
            TxBase::Context::Params pars;
			TxBase::Context ctx(pars);
			ctx.m_Height.m_Min = builder.GetMinHeight();
			if (!transaction->IsValid(ctx))
            {
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return;
            }
            GetGateway().register_tx(GetTxID(), transaction);
            SetState(State::Registration);
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
        else if (proto::TxStatus::Ok != nRegistered )
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
            return;
        }

        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            SetState(State::KernelConfirmation);
            ConfirmKernel(builder.GetKernelID());
            return;
        }

        SetCompletedTxCoinStatuses(hProof);

        CompleteTx();
    }

    void SimpleTransaction::SendInvitation(const BaseTxBuilder& builder, bool isSender)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::Amount, builder.GetAmount())
            .AddParameter(TxParameterID::Fee, builder.GetFee())
            .AddParameter(TxParameterID::MinHeight, builder.GetMinHeight())
            .AddParameter(TxParameterID::Lifetime, builder.GetLifetime())
            .AddParameter(TxParameterID::PeerMaxHeight, builder.GetMaxHeight())
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
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
            .AddParameter(TxParameterID::PeerMaxHeight, builder.GetMaxHeight());
        if (sendUtxos)
        {
            msg.AddParameter(TxParameterID::PeerInputs, builder.GetInputs())
            .AddParameter(TxParameterID::PeerOutputs, builder.GetOutputs())
            .AddParameter(TxParameterID::PeerOffset, builder.GetOffset());
        }

        assert(!IsSelfTx());
        if (!GetMandatoryParameter<bool>(TxParameterID::IsSender))
        {
            PaymentConfirmation pc;
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
                    
                    m_KeyKeeper->get_SbbsKdf()->DeriveKey(sk, Key::ID(waddr->m_OwnID, Key::Type::Bbs));

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
		uint8_t nCode = proto::TxStatus::Ok; // compiler workaround (ref to static const)
        msg.AddParameter(TxParameterID::TransactionRegistered, nCode);
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
