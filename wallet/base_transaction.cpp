// Copyright 2019 The Beam Team
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

#include "base_transaction.h"
#include "core/block_crypt.h"

// TODO: getrandom not available until API 28 in the Android NDK 17b
// https://github.com/boostorg/uuid/issues/76
#if defined(__ANDROID__)
#define BOOST_UUID_RANDOM_PROVIDER_DISABLE_GETRANDOM 1
#endif

#include <boost/uuid/uuid_generators.hpp>
#include <numeric>
#include "utility/logger.h"

namespace beam::wallet
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
        , m_Notify{ notify }
        , m_Reason{ reason }
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
            //    auto eventHolder = make_shared<io::AsyncEvent::Ptr>();
               // *eventHolder = io::AsyncEvent::create(io::Reactor::get_Current(), [eventHolder, result = move(result), cb = move(resultCallback)]() { cb(result); });
              //  (*eventHolder)->post();
            resultCallback(GenerateKeySync(ids, createCoinKey));
        }
        catch (const exception & ex)
        {
            //io::AsyncEvent::create(io::Reactor::get_Current(), [ex, cb = move(exceptionCallback)]() { cb(ex); })->post();
            exceptionCallback(ex);
        }
    }

    void LocalPrivateKeyKeeper::GenerateRangeProof(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<RangeProofs>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        try
        {
            resultCallback(GenerateRangeProofSync(schemeHeight, ids));
        }
        catch (const exception & ex)
        {
            exceptionCallback(ex);
        }
    }

    ////

    IPrivateKeyKeeper::PublicKeys LocalPrivateKeyKeeper::GenerateKeySync(const std::vector<Key::IDV>& ids, bool createCoinKey)
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
        return result;
    }

    IPrivateKeyKeeper::RangeProofs LocalPrivateKeyKeeper::GenerateRangeProofSync(Height schemeHeigh, const std::vector<Key::IDV>& ids)
    {
        RangeProofs result;
        Scalar::Native secretKey;
        Point commitment;
        result.reserve(ids.size());
        for (const auto& coinID : ids)
        {
            Output output;
            output.Create(schemeHeigh, secretKey, *GetChildKdf(coinID.m_SubIdx), coinID, *m_MasterKdf);

            assert(output.m_pConfidential);
            result.emplace_back(move(output.m_pConfidential));
        }
        return result;
    }

    IPrivateKeyKeeper::Nonce LocalPrivateKeyKeeper::GenerateNonceSync()
    {
        // Don't store the generated nonce for the kernel multisig. Instead - store the raw random, from which the nonce is derived using kdf.
        NoLeak<Hash::Value> hvRandom;
        ECC::GenRandom(hvRandom.V);
        NoLeak<Scalar::Native>& nonce = m_Nonces.emplace_back();
        m_MasterKdf->DeriveKey(nonce.V, hvRandom.V);

        Nonce result;
        result.m_Slot = static_cast<uint8_t>(m_Nonces.size());
        result.m_PublicValue = Context::get().G * nonce.V;
        return result;
    }

    Scalar LocalPrivateKeyKeeper::SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const Scalar& offset, uint8_t nonceSlot, const ECC::Hash::Value& message, const Point& peerPublicNonce, const Point& peerPublicExcess)
    {
        return Scalar();
    }

    ////

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
        , m_KeyKeeper{ keyKeeper }
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

    void BaseTransaction::UpdateAsync()
    {
        if (!m_EventToUpdate)
        {
            m_EventToUpdate = io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { UpdateImpl(); });
        }

        m_EventToUpdate->post();
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
        catch (const TransactionFailedException & ex)
        {
            LOG_ERROR() << GetTxID() << " exception msg: " << ex.what();
            OnFailed(ex.GetReason(), ex.ShouldNofify());
        }
        catch (const exception & ex)
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

    bool BaseTransaction::Rollback(Height height)
    {
        Height proofHeight;

        if (GetParameter(TxParameterID::KernelProofHeight, proofHeight) && (proofHeight > height))
        {
            SetParameter(TxParameterID::Status, TxStatus::Registering);
            SetParameter(TxParameterID::KernelProofHeight, 0);
            SetParameter(TxParameterID::KernelUnconfirmedHeight, 0);
            return true;
        }
        return false;
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
                || s == TxStatus::Completed))
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

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        Merkle::Hash kernelID;
        if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered)
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

    bool BaseTransaction::SendTxParameters(SetTxParameter && msg) const
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

    future<void> BaseTransaction::DoThreadAsync(Functor && functor, CompletionCallback && callback)
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
}