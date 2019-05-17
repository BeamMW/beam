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

    ///

    LocalPrivateKeyKeeper::LocalPrivateKeyKeeper(Key::IKdf::Ptr kdf)
        : m_MasterKdf(kdf)
    {
    }

    void LocalPrivateKeyKeeper::GenerateKey(const vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        try
        {
            PublicKeys result;
            Scalar::Native secretKey;
            result.reserve(ids.size());
            if (createCoinKey)
            {
                for (const auto& coinID : ids)
                {
                    Point& publicKey = result.emplace_back();
                    SwitchCommitment().Create(secretKey, publicKey, *GetChildKdf(coinID.m_SubIdx), coinID);
                }
            }
            else
            {
                for (const auto& keyID : ids)
                {
                    Point& publicKey = result.emplace_back();
                    m_MasterKdf->DeriveKey(secretKey, keyID);
                    publicKey = Context::get().G * secretKey;
                }
            }
        //    auto eventHolder = make_shared<io::AsyncEvent::Ptr>();
           // *eventHolder = io::AsyncEvent::create(io::Reactor::get_Current(), [eventHolder, result = move(result), cb = move(resultCallback)]() { cb(result); });
          //  (*eventHolder)->post();
            resultCallback(result);
        }
        catch (const exception & ex)
        {
            //io::AsyncEvent::create(io::Reactor::get_Current(), [ex, cb = move(exceptionCallback)]() { cb(ex); })->post();
            exceptionCallback(ex);
        }
    }

    void LocalPrivateKeyKeeper::GenerateBulletProof(const std::vector<Key::IDV>& ids, Callback<BulletProofs>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        try
        {
            BulletProofs result;
            Scalar::Native secretKey;
            Point commitment;
            result.reserve(ids.size());
            for (const auto& coinID : ids)
            {
                SwitchCommitment sc;
                sc.Create(secretKey, commitment, *GetChildKdf(coinID.m_SubIdx), coinID);

                ECC::Oracle oracle;
                oracle << 0U;// m_Incubation;

                ECC::RangeProof::CreatorParams cp;
                cp.m_Kidv = coinID;
                cp.m_Seed.V = GetSeedKid(*m_MasterKdf, commitment);

                auto& bulletProof = result.emplace_back(make_unique<ECC::RangeProof::Confidential>());
                bulletProof->Create(secretKey, cp, oracle, &sc.m_hGen);
            }
            resultCallback(result);
        }
        catch (const exception & ex)
        {
            exceptionCallback(ex);
        }
    }

    ECC::uintBig LocalPrivateKeyKeeper::GetSeedKid(Key::IPKdf& tagKdf, const Point& commitment) const
    {
        uintBig seed;
        ECC::Hash::Processor() << commitment >> seed;

        ECC::Scalar::Native sk;
        tagKdf.DerivePKey(sk, seed);

        ECC::Hash::Processor() << sk >> seed;
        return seed;
    }

    Key::IKdf::Ptr LocalPrivateKeyKeeper::GetChildKdf(Key::Index iKdf) const
    {
        if (!iKdf || m_MasterKdf)
            return m_MasterKdf; // by convention 0 is not a childd

        Key::IKdf::Ptr pRet;
        ECC::HKdf::CreateChild(pRet, *m_MasterKdf, iKdf);
        return pRet;
    }

    ///

    const uint32_t BaseTransaction::s_ProtoVersion = 3;


    BaseTransaction::BaseTransaction(INegotiatorGateway& gateway
                                   , IWalletDB::Ptr walletDB
                                   , IPrivateKeyKeeper::Ptr keyKeeper
                                   , const TxID& txID)
        : m_Gateway{ gateway }
        , m_WalletDB{ walletDB }
        , m_KeyKeeper{keyKeeper}
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
        AsyncContextHolder async(m_Gateway);
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
        // TODO: add CanCancel() method
        if (s == TxStatus::Pending || s == TxStatus::InProgress)
        {
            if (s == TxStatus::InProgress)
            {
                if (!m_KeyKeeper)
                {
                    // cannot create encrypted message
                    return;
                }
                // notify about cancellation if we have started negotiations
                NotifyFailure(TxFailureReason::Cancelled);

            }
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
        TxStatus s = TxStatus::Failed;
        if (GetParameter(TxParameterID::Status, s)
            && (s == TxStatus::Failed 
             || s == TxStatus::Cancelled 
             || s == TxStatus::Completed ))
        {
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

    void BaseTransaction::ConfirmKernel(const Merkle::Hash& kernelID)
    {
        UpdateTxDescription(TxStatus::Registering);
        m_Gateway.confirm_kernel(GetTxID(), kernelID);
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

    IPrivateKeyKeeper::Ptr BaseTransaction::GetKeyKeeper()
    {
        return m_KeyKeeper;
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

    future<void> BaseTransaction::DoThreadAsync(Functor&& functor, CompletionCallback&& callback)
    {
        weak_ptr<ITransaction> txRef = shared_from_this();
        m_Gateway.OnAsyncStarted();
        return do_thread_async(
            [functor = move(functor)]() 
            {
                functor();
            },
            [txRef, this, callback = move(callback)]()
            {
                if (auto txGuard = txRef.lock())
                {
                    callback();
                    Update();
                    m_Gateway.OnAsyncFinished();
                }
            });
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
            m_TxBuilder = make_shared<TxBuilder>(*this, amoutList, GetMandatoryParameter<Amount>(TxParameterID::Fee));
        }
        auto sharedBuilder = m_TxBuilder;
        TxBuilder& builder = *sharedBuilder;

        bool hasPeersInputsAndOutputs = builder.GetPeerInputsAndOutputs();

        if (!builder.LoadKernel() && !builder.HasKernelID())
        {
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

            if (!builder.GetInputs())
            {
                if (!builder.GetInputCoins().empty())
                {
                    builder.CreateInputs();
                    //return;
                }
            }

            if (!builder.GetOutputs())
            {
                if (!builder.GetOutputCoins().empty())
                {
                    //if (m_OutputsFuture.valid())
                    //{
                    //    return;
                    //}
                    //m_OutputsFuture = DoThreadAsync(
                    //    [sharedBuilder]()
                    //    {
                    //        sharedBuilder->CreateOutputs();
                    //    },
                    //    [sharedBuilder]()
                    //    {
                    //        if (!sharedBuilder->FinalizeOutputs())
                    //        {
                    //            //TODO: transaction is too big :(
                    //        }
                    //    });
                    builder.CreateOutputs();
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
        }

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
            ConfirmKernel(builder.GetKernelID());
            return;
        }

        vector<Coin> modified = m_WalletDB->getCoinsByTx(GetTxID());
        for (auto& coin : modified)
        {
            bool bIn = (coin.m_createTxId == m_ID);
            bool bOut = (coin.m_spentTxId == m_ID);
            if (bIn || bOut)
            {
                if (bIn)
                {
                    coin.m_confirmHeight = std::min(coin.m_confirmHeight, hProof);
                    coin.m_maturity = hProof + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
                }
                if (bOut)
                    coin.m_spentHeight = std::min(coin.m_spentHeight, hProof);
            }
        }

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
        , m_NonceSlot{0}
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

        m_InputCoins.reserve(coins.size());

        Amount total = 0;
        for (auto& coin : coins)
        {
            coin.m_spentTxId = m_Tx.GetTxID();
            total += coin.m_ID.m_Value;
            m_InputCoins.push_back(coin.m_ID);
        }

        m_Change += total - amountWithFee;

        m_Tx.SetParameter(TxParameterID::Change, m_Change, false);
        m_Tx.SetParameter(TxParameterID::InputCoins, m_InputCoins, false);

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
        Coin newUtxo{ amount };
        newUtxo.m_createTxId = m_Tx.GetTxID();
        if (bChange)
        {
            newUtxo.m_ID.m_Type = Key::Type::Change;
        }
        m_Tx.GetWalletDB()->store(newUtxo);
        m_OutputCoins.push_back(newUtxo.m_ID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false);
    }

    void TxBuilder::CreateOutputs()
    {
        for (const auto& coinID : m_OutputCoins)
        {
            Scalar::Native blindingFactor;
            Output::Ptr output = make_unique<Output>();
            output->Create(blindingFactor, *m_Tx.GetWalletDB()->get_ChildKdf(coinID.m_SubIdx), coinID, *m_Tx.GetWalletDB()->get_MasterKdf());
            m_Outputs.emplace_back(move(output));
        }

        auto thisHolder = shared_from_this();
        m_Tx.GetKeyKeeper()->GenerateKey(m_OutputCoins, true,
            [thisHolder, this](const auto & result)
            {
                m_Outputs.reserve(result.size());
                for (const auto& commitment : result)
                {
                    auto& output = m_Outputs.emplace_back(make_unique<Output>());
                    output->m_Commitment = commitment;
                }
                //FinalizeOutputs();
                //m_Tx.Update();
            },
            [thisHolder, this](const exception&)
            {
                //m_Tx.Update();
            });
    }

    bool TxBuilder::FinalizeOutputs()
    {
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false);
        
        // TODO: check transaction size here

        return true;
    }

    void TxBuilder::CreateInputs()
    {
        auto thisHolder = shared_from_this();
        m_Tx.GetKeyKeeper()->GenerateKey(m_InputCoins, true,
            [thisHolder, this](const auto & result)
            {
                m_Inputs.reserve(result.size());
                for (const auto& commitment : result)
                {
                    auto& input = m_Inputs.emplace_back(make_unique<Input>());
                    input->m_Commitment = commitment;
                }
                FinalizeInputs();
                //m_Tx.Update();
            },
            [thisHolder, this](const exception &)
            {
                //m_Tx.Update();
            });
    }

    void TxBuilder::FinalizeInputs()
    {
        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false);
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

    void TxBuilder::GenerateOffset()
    {
        m_Offset.GenRandomNnz();
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false);
        LOG_DEBUG() << m_Tx.GetTxID() << " Offset: " << Scalar(m_Offset);
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

        //if (!m_Tx.GetParameter(TxParameterID::NonceSlot, m_NonceSlot))
        //{
        //    // allocate nonce slot
        //    m_NonceSlot = AllocateNonceSlot();
        //    m_Tx.SetParameter(TxParameterID::NonceSlot, m_NonceSlot, false);
        //}
    }

    Scalar::Native TxBuilder::GetExcess() const
    {
        // Excess = Sum(input blinfing factors) - Sum(output blinfing factors) - offset
        Point commitment;
        Scalar::Native blindingFactor;
        Scalar::Native excess = m_Offset;
        for (const auto& coinID : m_OutputCoins)
        {
            m_Tx.GetWalletDB()->calcCommitment(blindingFactor, commitment, coinID);
            excess += blindingFactor;
        }
        excess = -excess;

        for (const auto& coinID : m_InputCoins)
        {
            m_Tx.GetWalletDB()->calcCommitment(blindingFactor, commitment, coinID);
            excess += blindingFactor;
        }
        return excess;
    }

    Point::Native TxBuilder::GetPublicExcess() const
    {
        // PublicExcess = Sum(inputs) - Sum(outputs) - offset * G - (Sum(input amounts) - Sum(output amounts)) * H
        Point::Native publicAmount = Zero;
        Amount amount = 0;
        for (const auto& cid : m_InputCoins)
        {
            amount += cid.m_Value;
        }
        AmountBig::AddTo(publicAmount, amount);
        amount = 0;
        publicAmount = -publicAmount;
        for (const auto& cid : m_OutputCoins)
        {
            amount += cid.m_Value;
        }
        AmountBig::AddTo(publicAmount, amount);

        Point::Native publicExcess = Context::get().G * m_Offset;
        {
            Point::Native commitment;
            
            for (const auto& output : m_Outputs)
            {
                if (commitment.Import(output->m_Commitment))
                {
                    publicExcess += commitment;
                }
            }

            publicExcess = -publicExcess;
            for (const auto& input : m_Inputs)
            {
                if (commitment.Import(input->m_Commitment))
                {
                    publicExcess += commitment;
                }
            }
        }
        publicExcess += publicAmount;
        LOG_DEBUG() << m_Tx.GetTxID() << " Public excess: " << publicExcess;
        return publicExcess;
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
        m_Tx.GetParameter(TxParameterID::InputCoins, m_InputCoins);
        m_Tx.GetParameter(TxParameterID::OutputCoins, m_OutputCoins);

        if (!m_Tx.GetParameter(TxParameterID::MinHeight, m_MinHeight))
        {
            // adjust min height, this allows create transaction when node is out of sync
            auto currentHeight = m_Tx.GetWalletDB()->getCurrentHeight();
            m_MinHeight = currentHeight;
            m_Tx.SetParameter(TxParameterID::MinHeight, m_MinHeight);
            Height maxResponseHeight = 0;
            if (m_Tx.GetParameter(TxParameterID::PeerResponseHeight, maxResponseHeight))
            {
                // adjust responce height, if min height din not set then then it should be equal to responce time
                m_Tx.SetParameter(TxParameterID::PeerResponseHeight, maxResponseHeight + currentHeight);
            }
        }
        m_Tx.GetParameter(TxParameterID::Lifetime, m_Lifetime);
        m_Tx.GetParameter(TxParameterID::PeerMaxHeight, m_PeerMaxHeight);

        return m_Tx.GetParameter(TxParameterID::Offset, m_Offset);
    }

    bool TxBuilder::GetInputs()
    {
        return m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs);
    }

    bool TxBuilder::GetOutputs()
    {
        return m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs);
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

        auto excess = GetExcess();
        m_MultiSig.SignPartial(m_PartialSignature, m_Message, excess);

        StoreKernelID();
    }

    void TxBuilder::FinalizeSignature()
    {
        // final signature
        m_Kernel->m_Signature.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        m_Kernel->m_Signature.m_k = m_PartialSignature + m_PeerSignature;
        
        StoreKernelID();
        m_Tx.SetParameter(TxParameterID::Kernel, m_Kernel);
    }

    bool TxBuilder::LoadKernel()
    {
        if (m_Tx.GetParameter(TxParameterID::Kernel, m_Kernel))
        {
            GetInitialTxParams();
            return true;
        }
        return false;
    }

    bool TxBuilder::HasKernelID() const
    {
        Merkle::Hash kernelID;
        return m_Tx.GetParameter(TxParameterID::KernelID, kernelID);
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

    const Merkle::Hash& TxBuilder::GetKernelID() const
    {
        if (!m_KernelID)
        {
            Merkle::Hash kernelID;
            if (m_Tx.GetParameter(TxParameterID::KernelID, kernelID))
            {
                m_KernelID = kernelID;
            }
            else
            {
                assert(false && "KernelID is not stored");
            }
            
        }
        return *m_KernelID;
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
        m_Tx.GetParameter(TxParameterID::KernelID, kernelID);
        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }

    bool TxBuilder::UpdateMaxHeight()
    {
        Merkle::Hash kernelId;
        if (!m_Tx.GetParameter(TxParameterID::MaxHeight, m_MaxHeight) &&
            !m_Tx.GetParameter(TxParameterID::KernelID, kernelId))
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
        Height lifetime = 0;
        Height peerResponceHeight = 0;
        if (!m_Tx.GetParameter(TxParameterID::Lifetime, lifetime)
         || !m_Tx.GetParameter(TxParameterID::PeerResponseHeight, peerResponceHeight))
        {
            // possible situation during update from older version
            return true; 
        }
        Height maxAcceptableHeight = lifetime + peerResponceHeight;
        return m_PeerMaxHeight < MaxHeight && m_PeerMaxHeight <= maxAcceptableHeight;
    }

    const std::vector<Coin::ID>& TxBuilder::GetInputCoins() const
    {
        return m_InputCoins;
    }

    const std::vector<Coin::ID>& TxBuilder::GetOutputCoins() const
    {
        return m_OutputCoins;
    }
}}
