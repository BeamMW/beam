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
#include "core/block_crypt.h"
#include <boost/uuid/uuid_generators.hpp>

namespace beam { namespace wallet
{
    using namespace ECC;
    using namespace std;


    TxID GenerateTxID()
    {
        boost::uuids::uuid id = boost::uuids::random_generator()();
        TxID txID{};
        copy(id.begin(), id.end(), txID.begin());
        return txID;
    }

    std::string GetFailureMessage(TxFailureReason reason)
    {
        switch (reason)
        {
#define MACRO(name, code, message) case name: return message;
            BEAM_TX_FAILURE_REASON_MAP(MACRO)
#undef MACRO
        }
        return "Unknown reason";
    }

    TransactionFailedException::TransactionFailedException(bool notify, TxFailureReason reason, const char* message)
        : std::runtime_error(message)
        , m_Notify{notify}
        , m_Reason{reason}
    {

    }
    bool TransactionFailedException::ShouldNofify() const
    {
        return m_Notify;
    }

    TxFailureReason TransactionFailedException::GetReason() const
    {
        return m_Reason;
    }

    BaseTransaction::BaseTransaction(INegotiatorGateway& gateway
                                   , beam::IWalletDB::Ptr walletDB
                                   , const TxID& txID)
        : m_Gateway{ gateway }
        , m_WalletDB{ walletDB }
        , m_ID{ txID }
    {
        assert(walletDB);
    }

    bool BaseTransaction::IsInitiator() const
    {
        if (!m_IsInitiator.is_initialized())
        {
            m_IsInitiator = GetMandatoryParameter<bool>(TxParameterID::IsInitiator);
        }
        return *m_IsInitiator;
    }

    bool BaseTransaction::GetTip(Block::SystemState::Full& state) const
    {
        return m_Gateway.get_tip(state);
    }

    const TxID& BaseTransaction::GetTxID() const
    {
        return m_ID;
    }

    void BaseTransaction::Update()
    {
        try
        {
            TxFailureReason reason = TxFailureReason::Unknown;
            if (GetParameter(TxParameterID::FailureReason, reason))
            {
                OnFailed(reason);
                return;
            }

            WalletID myID = GetMandatoryParameter<WalletID>(TxParameterID::MyID);
            auto address = m_WalletDB->getAddress(myID);
            if (address.is_initialized() && address->isExpired())
            {
                OnFailed(TxFailureReason::ExpiredAddressProvided);
                return;
            }

            UpdateImpl();
        }
        catch (const TransactionFailedException& ex)
        {
            LOG_ERROR() << GetTxID() << " exception msg: " << ex.what();
            OnFailed(ex.GetReason(), ex.ShouldNofify());
        }
        catch (const exception& ex)
        {
            LOG_ERROR() << GetTxID() << " exception msg: " << ex.what();
        }
    }

    void BaseTransaction::Cancel()
    {
        TxStatus s = TxStatus::Failed;
        GetParameter(TxParameterID::Status, s);
        if (s == TxStatus::Pending)
        {
            m_WalletDB->deleteTx(GetTxID());
        }
        else
        {
            UpdateTxDescription(TxStatus::Cancelled);
            RollbackTx();
            SetTxParameter msg;
            msg.AddParameter(TxParameterID::FailureReason, TxFailureReason::Cancelled);
            SendTxParameters(move(msg));
            m_Gateway.on_tx_completed(GetTxID());
        }
    }

    void BaseTransaction::RollbackTx()
    {
        LOG_INFO() << GetTxID() << " Transaction failed. Rollback...";
        m_WalletDB->rollbackTx(GetTxID());
    }

    void BaseTransaction::ConfirmKernel(const TxKernel& kernel)
    {
        UpdateTxDescription(TxStatus::Registered);
        m_Gateway.confirm_kernel(GetTxID(), kernel);
    }

    void BaseTransaction::CompleteTx()
    {
        LOG_INFO() << GetTxID() << " Transaction completed";
        UpdateTxDescription(TxStatus::Completed);
        m_Gateway.on_tx_completed(GetTxID());
    }

    void BaseTransaction::UpdateTxDescription(TxStatus s)
    {
        SetParameter(TxParameterID::Status, s, true);
        SetParameter(TxParameterID::ModifyTime, getTimestamp(), false);
    }

    void BaseTransaction::OnFailed(TxFailureReason reason, bool notify)
    {
        LOG_ERROR() << GetTxID() << " Failed. " << GetFailureMessage(reason);
        UpdateTxDescription((reason == TxFailureReason::Cancelled) ? TxStatus::Cancelled : TxStatus::Failed);
        RollbackTx();
        if (notify)
        {
            SetTxParameter msg;
            msg.AddParameter(TxParameterID::FailureReason, reason);
            SendTxParameters(move(msg));
        }
        m_Gateway.on_tx_completed(GetTxID());
    }

    IWalletDB::Ptr BaseTransaction::GetWalletDB()
    {
        return m_WalletDB;
    }

    vector<Coin> BaseTransaction::GetUnconfirmedOutputs() const
    {
        vector<Coin> outputs;
        m_WalletDB->visit([&](const Coin& coin)
        {
            if ((coin.m_createTxId == GetTxID() && (coin.m_status == Coin::Incoming || coin.m_status == Coin::Change))
                || (coin.m_spentTxId == GetTxID() && coin.m_status == Coin::Outgoing))
            {
                outputs.emplace_back(coin);
            }

            return true;
        });
        return outputs;
    }

    bool BaseTransaction::SendTxParameters(SetTxParameter&& msg) const
    {
        msg.m_TxID = GetTxID();
        msg.m_Type = GetType();
        
        WalletID peerID;
        if (GetParameter(TxParameterID::MyID, msg.m_From) 
            && GetParameter(TxParameterID::PeerID, peerID))
        {
            m_Gateway.send_tx_params(peerID, move(msg));
            return true;
        }
        return false;
    }

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
        TxBuilder builder{ *this, GetMandatoryParameter<Amount>(TxParameterID::Amount), GetMandatoryParameter<Amount>(TxParameterID::Fee) };
        if (!builder.GetInitialTxParams() && txState == State::Initial)
        {
            LOG_INFO() << GetTxID() << (isSender ? " Sending " : " Receiving ") << PrintableAmount(builder.GetAmount()) << " (fee: " << PrintableAmount(builder.GetFee()) << ")";

            if (isSender)
            {
                builder.SelectInputs();
                builder.AddChangeOutput();
            }

            if (isSelfTx || !isSender)
            {
                // create receiver utxo
                builder.AddOutput(builder.GetAmount(), Coin::Incoming);

                LOG_INFO() << GetTxID() << " Invitation accepted";
            }
            UpdateTxDescription(TxStatus::InProgress);
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

        if (!isSelfTx && !builder.GetPeerSignature())
        {
            if (txState == State::Initial)
            {
                // invited participant
                assert(!IsInitiator());
                
                UpdateTxDescription(TxStatus::Registered);
                ConfirmInvitation(builder);
                SetState(State::InvitationConfirmation);
            }
            return;
        }

        if (!builder.IsPeerSignatureValid())
        {
            OnFailed(TxFailureReason::InvalidPeerSignature, true);
            return;
        }

        builder.FinalizeSignature();

        bool isRegistered = false;
        if (!GetParameter(TxParameterID::TransactionRegistered, isRegistered))
        {
            if (!isSelfTx && !builder.GetPeerInputsAndOutputs())
            {
                assert(IsInitiator());
                if (txState == State::Invitation)
                {
                    UpdateTxDescription(TxStatus::Registered);
                    ConfirmTransaction(builder);
                    SetState(State::PeerConfirmation);
                }
            }
            else
            {
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
            }
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
            if (!IsInitiator() && txState == State::Registration)
            {
                // notify peer that transaction has been registered
                NotifyTransactionRegistered();
            }
            SetState(State::KernelConfirmation);
            ConfirmKernel(builder.GetKernel());
            return;
        }


        vector<Coin> unconfirmed = GetUnconfirmedOutputs();

        // Current design: don't request separate proofs for coins. Tx confirmation is enough.
        for (Coin& c : unconfirmed)
        {
            if (Coin::Outgoing == c.m_status)
            {
                c.m_status = Coin::Spent;
            }
            else
            {
                c.m_status = Coin::Available;
                c.m_confirmHeight = hProof;
                c.m_maturity = hProof + Rules::get().MaturityStd; // so far we don't use incubation for our created outputs
            }
        }

        GetWalletDB()->save(unconfirmed);

        CompleteTx();
    }

    void SimpleTransaction::SendInvitation(const TxBuilder& builder, bool isSender)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::Amount, builder.GetAmount())
            .AddParameter(TxParameterID::Fee, builder.GetFee())
            .AddParameter(TxParameterID::MinHeight, builder.GetMinHeight())
            .AddParameter(TxParameterID::MaxHeight, builder.GetMaxHeight())
            .AddParameter(TxParameterID::IsSender, !isSender)
            .AddParameter(TxParameterID::PeerInputs, builder.GetInputs())
            .AddParameter(TxParameterID::PeerOutputs, builder.GetOutputs())
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
            .AddParameter(TxParameterID::PeerOffset, builder.GetOffset());

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void SimpleTransaction::ConfirmInvitation(const TxBuilder& builder)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerSignature, builder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce());
        SendTxParameters(move(msg));
    }

    void SimpleTransaction::ConfirmTransaction(const TxBuilder& builder)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::PeerSignature, Scalar(builder.GetPartialSignature()));
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

    bool SimpleTransaction::isShouldNotifyAboutChanges(TxParameterID paramID) const
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
            return true;
        default:
            return false;
        }
    }

    TxBuilder::TxBuilder(BaseTransaction& tx, Amount amount, Amount fee)
        : m_Tx{ tx }
        , m_Amount{ amount }
        , m_Fee{ fee }
        , m_Change{0}
        , m_MinHeight{0}
        , m_MaxHeight{MaxHeight}
    {
    }

    void TxBuilder::SelectInputs()
    {
        Amount amountWithFee = m_Amount + m_Fee;
        auto coins = m_Tx.GetWalletDB()->selectCoins(amountWithFee);
        if (coins.empty())
        {
            LOG_ERROR() << "You only have " << PrintableAmount(getAvailable(m_Tx.GetWalletDB()));
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoInputs);
        }

        m_Inputs.reserve(m_Inputs.size() + coins.size());
        Amount total = 0;
        for (auto& coin : coins)
        {
            coin.m_spentTxId = m_Tx.GetTxID();

            auto& input = m_Inputs.emplace_back(make_unique<Input>());

			Scalar::Native blindingFactor;
			m_Tx.GetWalletDB()->calcCommitment(blindingFactor, input->m_Commitment, coin.m_ID);

            m_Offset += blindingFactor;
            total += coin.m_ID.m_Value;
        }

        m_Change += total - amountWithFee;

        m_Tx.SetParameter(TxParameterID::Change, m_Change, false);
        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false);
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false);

        m_Tx.GetWalletDB()->save(coins);
    }

    void TxBuilder::AddChangeOutput()
    {
        if (m_Change == 0)
        {
            return;
        }

        AddOutput(m_Change, Coin::Change);
    }

    void TxBuilder::AddOutput(Amount amount, Coin::Status status)
    {
        m_Outputs.push_back(CreateOutput(amount, status, m_MinHeight));
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false);
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false);
    }

    Output::Ptr TxBuilder::CreateOutput(Amount amount, Coin::Status status, bool shared, Height incubation)
    {
        Coin newUtxo{ amount, status };
        newUtxo.m_createTxId = m_Tx.GetTxID();
        newUtxo.m_createHeight = m_MinHeight;
        if (Coin::Status::Change == status)
            newUtxo.m_ID.m_Type = Key::Type::Change;
        m_Tx.GetWalletDB()->store(newUtxo);

        Scalar::Native blindingFactor;
        Output::Ptr output = make_unique<Output>();
        output->Create(blindingFactor, *m_Tx.GetWalletDB()->get_ChildKdf(newUtxo.m_ID.m_iChild), newUtxo.m_ID);

        blindingFactor = -blindingFactor;
        m_Offset += blindingFactor;

        return output;
    }

    void TxBuilder::CreateKernel()
    {
        // create kernel
        assert(!m_Kernel);
        m_Kernel = make_unique<TxKernel>();
        m_Kernel->m_Fee = m_Fee;
        m_Kernel->m_Height.m_Min = m_MinHeight;
        m_Kernel->m_Height.m_Max = m_MaxHeight;
        m_Kernel->m_Commitment = Zero;

        if (!m_Tx.GetParameter(TxParameterID::BlindingExcess, m_BlindingExcess))
        {
			Key::ID kid;
			kid.m_Idx = m_Tx.GetWalletDB()->AllocateKidRange(1);
			kid.m_Type = FOURCC_FROM(KerW);

			m_Tx.GetWalletDB()->get_MasterKdf()->DeriveKey(m_BlindingExcess, kid);

            m_Tx.SetParameter(TxParameterID::BlindingExcess, m_BlindingExcess, false);
        }

        m_Offset += m_BlindingExcess;
        m_BlindingExcess = -m_BlindingExcess;

        if (!m_Tx.GetParameter(TxParameterID::MyNonce, m_MultiSig.m_Nonce))
        {
            m_MultiSig.m_Nonce.GenRandomNnz();
            m_Tx.SetParameter(TxParameterID::MyNonce, m_MultiSig.m_Nonce, false);
        }
    }

    Point::Native TxBuilder::GetPublicExcess() const
    {
        return Context::get().G * m_BlindingExcess;
    }

    Point::Native TxBuilder::GetPublicNonce() const
    {
        return Context::get().G * m_MultiSig.m_Nonce;
    }

    bool TxBuilder::GetPeerPublicExcessAndNonce()
    {
        return m_Tx.GetParameter(TxParameterID::PeerPublicExcess, m_PeerPublicExcess)
            && m_Tx.GetParameter(TxParameterID::PeerPublicNonce, m_PeerPublicNonce);
    }

    bool TxBuilder::GetPeerSignature()
    {
        if (m_Tx.GetParameter(TxParameterID::PeerSignature, m_PeerSignature))
        {
            LOG_DEBUG() << "Received PeerSig:\t" << Scalar(m_PeerSignature);
            return true;
        }
        
        return false;
    }

    bool TxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs);
        m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs);
        m_Tx.GetParameter(TxParameterID::MinHeight, m_MinHeight);
        m_Tx.GetParameter(TxParameterID::MaxHeight, m_MaxHeight);
        return m_Tx.GetParameter(TxParameterID::BlindingExcess, m_BlindingExcess)
            && m_Tx.GetParameter(TxParameterID::Offset, m_Offset);
    }

    bool TxBuilder::GetPeerInputsAndOutputs()
    {
        return m_Tx.GetParameter(TxParameterID::PeerInputs, m_PeerInputs)
            && m_Tx.GetParameter(TxParameterID::PeerOutputs, m_PeerOutputs)
            && m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset);
    }

    void TxBuilder::SignPartial()
    {
        // create signature
        Point::Native totalPublicExcess = GetPublicExcess();
        totalPublicExcess += m_PeerPublicExcess;
        m_Kernel->m_Commitment = totalPublicExcess;

        m_Kernel->get_Hash(m_Message);
        m_MultiSig.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        
        
        m_MultiSig.SignPartial(m_PartialSignature, m_Message, m_BlindingExcess);
    }

    void TxBuilder::FinalizeSignature()
    {
        // final signature
        m_Kernel->m_Signature.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        m_Kernel->m_Signature.m_k = m_PartialSignature + m_PeerSignature;
    }

    Transaction::Ptr TxBuilder::CreateTransaction()
    {
        assert(m_Kernel);
        Merkle::Hash kernelID = { 0 };
        m_Kernel->get_ID(kernelID);
        LOG_INFO() << m_Tx.GetTxID() << " Transaction kernel: " << kernelID;
        // create transaction
        auto transaction = make_shared<Transaction>();
        transaction->m_vKernels.push_back(move(m_Kernel));
        transaction->m_Offset = m_Offset + m_PeerOffset;
        transaction->m_vInputs = move(m_Inputs);
        transaction->m_vOutputs = move(m_Outputs);
        move(m_PeerInputs.begin(), m_PeerInputs.end(), back_inserter(transaction->m_vInputs));
        move(m_PeerOutputs.begin(), m_PeerOutputs.end(), back_inserter(transaction->m_vOutputs));

        transaction->Normalize();

        return transaction;
    }

    bool TxBuilder::IsPeerSignatureValid() const
    {
        Signature peerSig;
        peerSig.m_NoncePub = m_MultiSig.m_NoncePub;
        peerSig.m_k = m_PeerSignature;
        return peerSig.IsValidPartial(m_Message, m_PeerPublicNonce, m_PeerPublicExcess);
    }

    Amount TxBuilder::GetAmount() const
    {
        return m_Amount;
    }

    Amount TxBuilder::GetFee() const
    {
        return m_Fee;
    }

    Height TxBuilder::GetMinHeight() const
    {
        return m_MinHeight;
    }

    Height TxBuilder::GetMaxHeight() const
    {
        return m_MaxHeight;
    }

    const vector<Input::Ptr>& TxBuilder::GetInputs() const
    {
        return m_Inputs;
    }

    const vector<Output::Ptr>& TxBuilder::GetOutputs() const
    {
        return m_Outputs;
    }

    const Scalar::Native& TxBuilder::GetOffset() const
    {
        return m_Offset;
    }

    const Scalar::Native& TxBuilder::GetPartialSignature() const
    {
        return m_PartialSignature;
    }

    const TxKernel& TxBuilder::GetKernel() const
    {
        assert(m_Kernel);
        return *m_Kernel;
    }
}}
