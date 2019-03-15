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

// TODO: getrandom not available until API 28 in the Android NDK 17b
// https://github.com/boostorg/uuid/issues/76
#if defined(__ANDROID__)
#define BOOST_UUID_RANDOM_PROVIDER_DISABLE_GETRANDOM 1
#endif

#include <boost/uuid/uuid_generators.hpp>
#include <numeric>
#include "utility/logger.h"

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

    const uint32_t BaseTransaction::s_ProtoVersion = 1;


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

    uint32_t BaseTransaction::get_PeerVersion() const
    {
        uint32_t nVer = 0;
        GetParameter(TxParameterID::PeerProtoVersion, nVer);
        return nVer;
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
            if (CheckExternalFailures())
            {
                return;
            }

            UpdateImpl();

            CheckExpired();
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
        if (s == TxStatus::Pending || s == TxStatus::InProgress)
        {
            NotifyFailure(TxFailureReason::Cancelled);
            UpdateTxDescription(TxStatus::Cancelled);
            RollbackTx();
            m_Gateway.on_tx_completed(GetTxID());
        }
        else
        {
            LOG_INFO() << GetTxID() << " You cannot cancel transaction in state: " << static_cast<int>(s);
        }
    }

    void BaseTransaction::RollbackTx()
    {
        LOG_INFO() << GetTxID() << " Transaction failed. Rollback...";
        m_WalletDB->rollbackTx(GetTxID());
    }

    bool BaseTransaction::CheckExpired()
    {
        Height kernelConfirmHeight = 0;
        if (GetParameter(TxParameterID::KernelProofHeight, kernelConfirmHeight) && kernelConfirmHeight > 0)
        {
            // completed tx
            return false;
        }

        TxFailureReason reason = TxFailureReason::Unknown;
        if (GetParameter(TxParameterID::FailureReason, reason))
        {
            // failed tx
            return false;
        }

        Height maxHeight = MaxHeight;
        if (!GetParameter(TxParameterID::MaxHeight, maxHeight) 
         && !GetParameter(TxParameterID::PeerResponseHeight, maxHeight))
        {
            // we have no data to make decision
            return false;
        }

        bool isRegistered = false;
        Merkle::Hash kernelID;
        if (!GetParameter(TxParameterID::TransactionRegistered, isRegistered)
         || !GetParameter(TxParameterID::KernelID, kernelID))
        {
            Block::SystemState::Full state;
            if (GetTip(state) && state.m_Height > maxHeight)
            {
                LOG_INFO() << GetTxID() << " Transaction expired. Current height: " << state.m_Height << ", max kernel height: " << maxHeight;
                OnFailed(TxFailureReason::TransactionExpired);
                return true;
            }
        }
        else
        {
            Height lastUnconfirmedHeight = 0;
            if (GetParameter(TxParameterID::KernelUnconfirmedHeight, lastUnconfirmedHeight) && lastUnconfirmedHeight > 0)
            {
                if (lastUnconfirmedHeight >= maxHeight)
                {
                    LOG_INFO() << GetTxID() << " Transaction expired. Last unconfirmeed height: " << lastUnconfirmedHeight << ", max kernel height: " << maxHeight;
                    OnFailed(TxFailureReason::TransactionExpired);
                    return true;
                }
            }
        }
        return false;
    }

    bool BaseTransaction::CheckExternalFailures()
    {
        TxFailureReason reason = TxFailureReason::Unknown;
        if (GetParameter(TxParameterID::FailureReason, reason))
        {
            TxStatus s = GetMandatoryParameter<TxStatus>(TxParameterID::Status);
            if (s == TxStatus::InProgress) 
            {
                OnFailed(reason);
                return true;
            }
        }
        return false;
    }

    void BaseTransaction::ConfirmKernel(const TxKernel& kernel)
    {
        UpdateTxDescription(TxStatus::Registering);
        m_Gateway.confirm_kernel(GetTxID(), kernel);
    }

    void BaseTransaction::UpdateOnNextTip()
    {
        m_Gateway.UpdateOnNextTip(GetTxID());
    }

    void BaseTransaction::CompleteTx()
    {
        LOG_INFO() << GetTxID() << " Transaction completed";
        UpdateTxDescription(TxStatus::Completed);
        m_Gateway.on_tx_completed(GetTxID());
    }

    void BaseTransaction::UpdateTxDescription(TxStatus s)
    {
        if (SetParameter(TxParameterID::Status, s, true))
        {
            SetParameter(TxParameterID::ModifyTime, getTimestamp(), false);
        }
    }

    void BaseTransaction::OnFailed(TxFailureReason reason, bool notify)
    {
        LOG_ERROR() << GetTxID() << " Failed. " << GetFailureMessage(reason);

        if (notify)
        {
            NotifyFailure(reason);
        }

        SetParameter(TxParameterID::FailureReason, reason, false);
        UpdateTxDescription((reason == TxFailureReason::Cancelled) ? TxStatus::Cancelled : TxStatus::Failed);
        RollbackTx();

        m_Gateway.on_tx_completed(GetTxID());
    }

    void BaseTransaction::NotifyFailure(TxFailureReason reason)
    {
        TxStatus s = TxStatus::Failed;
        GetParameter(TxParameterID::Status, s);

        switch (s)
        {
        case TxStatus::Pending:
        case TxStatus::InProgress:
            // those are the only applicable statuses, where there's no chance tx can be valid
            break;
        default:
            return;
        }

        SetTxParameter msg;
        msg.AddParameter(TxParameterID::FailureReason, reason);
        SendTxParameters(move(msg));
    }

    IWalletDB::Ptr BaseTransaction::GetWalletDB()
    {
        return m_WalletDB;
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
        AmountList amoutList;
        if (!GetParameter(TxParameterID::AmountList, amoutList))
        {
            amoutList = AmountList{ GetMandatoryParameter<Amount>(TxParameterID::Amount) };
        }

        auto sharedBuilder = make_shared<TxBuilder>(*this, amoutList, GetMandatoryParameter<Amount>(TxParameterID::Fee));
        TxBuilder& builder = *sharedBuilder;

        builder.GenerateBlindingExcess();
        if (!builder.GetInitialTxParams() && txState == State::Initial)
        {
            if (m_CompletedEvent)
            {
                return;
            }
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
            
            if (!builder.GetCoins().empty())
            {
                m_CompletedEvent = io::AsyncEvent::create(io::Reactor::get_Current(), [this, sharedBuilder]() mutable
                {
                    if (!sharedBuilder->FinalizeOutputs())
                    {
                        //TODO: transaction is too big :(
                    }
                    Update();
                });
                m_OutputsFuture = async(launch::async, [this, sharedBuilder]() mutable
                {
                    sharedBuilder->CreateOutputs();
                    m_CompletedEvent->post();
                });
                return;
            }
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

            if (CheckExpired())
            {
                return;
            }

            // Construct transaction
            auto transaction = builder.CreateTransaction();

            // Verify final transaction
            TxBase::Context::Params pars;
			TxBase::Context ctx(pars);
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

    void SimpleTransaction::SendInvitation(const TxBuilder& builder, bool isSender)
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

    void SimpleTransaction::ConfirmInvitation(const TxBuilder& builder, bool sendUtxos)
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

    void SimpleTransaction::ConfirmTransaction(const TxBuilder& builder, bool sendUtxos)
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

    TxBuilder::TxBuilder(BaseTransaction& tx, const AmountList& amountList, Amount fee)
        : m_Tx{ tx }
        , m_AmountList{ amountList }
        , m_Fee{ fee }
        , m_Change{0}
        , m_Lifetime{120}
        , m_MinHeight{0}
        , m_MaxHeight{MaxHeight}
        , m_PeerMaxHeight{ MaxHeight }
    {
    }

    void TxBuilder::SelectInputs()
    {
        CoinIDList preselectedCoinIDs;
        vector<Coin> coins;
        Amount preselectedAmount = 0;
        if (m_Tx.GetParameter(TxParameterID::PreselectedCoins, preselectedCoinIDs) && !preselectedCoinIDs.empty())
        {
            coins = m_Tx.GetWalletDB()->getCoinsByID(preselectedCoinIDs);
            for (auto& coin : coins)
            {
                preselectedAmount += coin.getAmount();
                coin.m_spentTxId = m_Tx.GetTxID();
            }
            m_Tx.GetWalletDB()->save(coins);
        }
        Amount amountWithFee = GetAmount() + m_Fee;
        if (preselectedAmount < amountWithFee)
        {
            auto selectedCoins = m_Tx.GetWalletDB()->selectCoins(amountWithFee - preselectedAmount);
            copy(selectedCoins.begin(), selectedCoins.end(), back_inserter(coins));
        }

        if (coins.empty())
        {
            Totals totals(*m_Tx.GetWalletDB());

            LOG_ERROR() << m_Tx.GetTxID() << " You only have " << PrintableAmount(totals.Avail);
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

    void TxBuilder::AddChange()
    {
        if (m_Change == 0)
        {
            return;
        }

        GenerateNewCoin(m_Change, true);
    }

    void TxBuilder::GenerateNewCoin(Amount amount, bool bChange)
    {
        Coin& newUtxo = m_Coins.emplace_back(amount);
        newUtxo.m_createTxId = m_Tx.GetTxID();
        if (bChange)
        {
            newUtxo.m_ID.m_Type = Key::Type::Change;
        }
        m_Tx.GetWalletDB()->store(newUtxo);
    }

    void TxBuilder::CreateOutputs()
    {
        for (const auto& utxo : m_Coins)
        {
            Scalar::Native blindingFactor;
            Output::Ptr output = make_unique<Output>();
            output->Create(blindingFactor, *m_Tx.GetWalletDB()->get_ChildKdf(utxo.m_ID.m_SubIdx), utxo.m_ID, *m_Tx.GetWalletDB()->get_MasterKdf());

            blindingFactor = -blindingFactor;
            m_Offset += blindingFactor;
            m_Outputs.emplace_back(move(output));
        }
    }

    void TxBuilder::AddOutput(Amount amount, bool bChange)
    {
        m_Outputs.push_back(CreateOutput(amount, bChange));
    }

    bool TxBuilder::FinalizeOutputs()
    {
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false);
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false);
        
        // TODO: check transaction size here

        return true;
    }

    Output::Ptr TxBuilder::CreateOutput(Amount amount, bool bChange)
    {
        Coin newUtxo(amount);
        newUtxo.m_createTxId = m_Tx.GetTxID();
        if (bChange)
        {
            newUtxo.m_ID.m_Type = Key::Type::Change;
        }
        m_Tx.GetWalletDB()->store(newUtxo);

        Scalar::Native blindingFactor;
        Output::Ptr output = make_unique<Output>();
        output->Create(blindingFactor, *m_Tx.GetWalletDB()->get_ChildKdf(newUtxo.m_ID.m_SubIdx), newUtxo.m_ID, *m_Tx.GetWalletDB()->get_MasterKdf());

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
        m_Kernel->m_Height.m_Min = GetMinHeight();
        m_Kernel->m_Height.m_Max = GetMaxHeight();
        m_Kernel->m_Commitment = Zero;

        m_Tx.SetParameter(TxParameterID::MaxHeight, GetMaxHeight());
    }

    void TxBuilder::GenerateBlindingExcess()
    {
        if (!m_Tx.GetParameter(TxParameterID::BlindingExcess, m_BlindingExcess))
        {
            Key::ID kid;
            kid.m_Idx = m_Tx.GetWalletDB()->AllocateKidRange(1);
            kid.m_Type = FOURCC_FROM(KerW);
            kid.m_SubIdx = 0;

            m_Tx.GetWalletDB()->get_MasterKdf()->DeriveKey(m_BlindingExcess, kid);

            m_Tx.SetParameter(TxParameterID::BlindingExcess, m_BlindingExcess, false);
        }

        m_Offset += m_BlindingExcess;
        m_BlindingExcess = -m_BlindingExcess;
    }

    void TxBuilder::GenerateNonce()
    {
         // Don't store the generated nonce for the kernel multisig. Instead - store the raw random, from which the nonce is derived using kdf.
        NoLeak<Hash::Value> hvRandom;
        if (!m_Tx.GetParameter(TxParameterID::MyNonce, hvRandom.V))
        {
            ECC::GenRandom(hvRandom.V);
            m_Tx.SetParameter(TxParameterID::MyNonce, hvRandom.V, false);
        }

        m_Tx.GetWalletDB()->get_MasterKdf()->DeriveKey(m_MultiSig.m_Nonce, hvRandom.V);
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
            LOG_DEBUG() << m_Tx.GetTxID() << " Received PeerSig:\t" << Scalar(m_PeerSignature);
            return true;
        }
        
        return false;
    }

    bool TxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs);
        m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs);
        m_Tx.GetParameter(TxParameterID::MinHeight, m_MinHeight);
        m_Tx.GetParameter(TxParameterID::Lifetime, m_Lifetime);
        m_Tx.GetParameter(TxParameterID::PeerMaxHeight, m_PeerMaxHeight);

        return m_Tx.GetParameter(TxParameterID::Offset, m_Offset);
    }

    bool TxBuilder::GetPeerInputsAndOutputs()
    {
        // used temporary vars to avoid non-short circuit evaluation
        bool hasInputs = m_Tx.GetParameter(TxParameterID::PeerInputs, m_PeerInputs);
        bool hasOutputs = (m_Tx.GetParameter(TxParameterID::PeerOutputs, m_PeerOutputs)
                        && m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset));
        return hasInputs || hasOutputs;
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

        StoreKernelID();
    }

    void TxBuilder::FinalizeSignature()
    {
        // final signature
        m_Kernel->m_Signature.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        m_Kernel->m_Signature.m_k = m_PartialSignature + m_PeerSignature;
        
        StoreKernelID();
    }

    Transaction::Ptr TxBuilder::CreateTransaction()
    {
        assert(m_Kernel);
        LOG_INFO() << m_Tx.GetTxID() << " Transaction created. Kernel: " << GetKernelIDString()
            << " min height: " << m_Kernel->m_Height.m_Min
            << " max height: " << m_Kernel->m_Height.m_Max;

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
        return std::accumulate(m_AmountList.begin(), m_AmountList.end(), 0ULL);
    }

    const AmountList& TxBuilder::GetAmountList() const
    {
        return m_AmountList;
    }

    Amount TxBuilder::GetFee() const
    {
        return m_Fee;
    }

    Height TxBuilder::GetLifetime() const
    {
        return m_Lifetime;
    }

    Height TxBuilder::GetMinHeight() const
    {
        return m_MinHeight;
    }

    Height TxBuilder::GetMaxHeight() const
    {
        if (m_MaxHeight == MaxHeight)
        {
            return m_MinHeight + m_Lifetime;
        }
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

    void TxBuilder::StoreKernelID()
    {
        assert(m_Kernel);
        Merkle::Hash kernelID;
        m_Kernel->get_ID(kernelID);

        m_Tx.SetParameter(TxParameterID::KernelID, kernelID);
    }

    string TxBuilder::GetKernelIDString() const
    {
        Merkle::Hash kernelID;
        m_Kernel->get_ID(kernelID);

        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }

    bool TxBuilder::UpdateMaxHeight()
    {
        if (!m_Tx.GetParameter(TxParameterID::MaxHeight, m_MaxHeight))
        {
            bool isInitiator = m_Tx.IsInitiator();
            bool hasPeerMaxHeight = m_PeerMaxHeight < MaxHeight;
            if (!isInitiator)
            {
                if (m_Tx.GetParameter(TxParameterID::Lifetime, m_Lifetime))
                {
                    Block::SystemState::Full state;
                    if (m_Tx.GetTip(state))
                    {
                        m_MaxHeight = state.m_Height + m_Lifetime;
                    }
                }
                else if (hasPeerMaxHeight)
                {
                    m_MaxHeight = m_PeerMaxHeight;
                }
            }
            else if (hasPeerMaxHeight)
            {
                if (IsAcceptableMaxHeight())
                {
                    m_MaxHeight = m_PeerMaxHeight;
                }
                else
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool TxBuilder::IsAcceptableMaxHeight() const
    {
        Height maxAcceptableHeight = m_Tx.GetMandatoryParameter<Height>(TxParameterID::PeerResponseHeight) + 
                                     m_Tx.GetMandatoryParameter<Height>(TxParameterID::Lifetime);
        return m_PeerMaxHeight < MaxHeight && m_PeerMaxHeight <= maxAcceptableHeight;
    }

    const std::vector<Coin>& TxBuilder::GetCoins() const
    {
        return m_Coins;
    }
}}
