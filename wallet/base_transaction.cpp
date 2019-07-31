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

    namespace
    {
        const char* LOCAL_NONCE_SEEDS = "NonceSeeds";
        const size_t kMaxNonces = 1000000;
    }

    LocalPrivateKeyKeeper::LocalPrivateKeyKeeper(IWalletDB::Ptr walletDB)
        : m_WalletDB(walletDB)
        , m_MasterKdf(walletDB->get_MasterKdf())
    {
        LoadNonceSeeds();
    }

    void LocalPrivateKeyKeeper::GeneratePublicKeys(const vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        try
        {
            resultCallback(GeneratePublicKeysSync(ids, createCoinKey));
        }
        catch (const exception & ex)
        {
            exceptionCallback(ex);
        }
    }

    void LocalPrivateKeyKeeper::GenerateOutputs(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        auto thisHolder = shared_from_this();
        shared_ptr<Outputs> result = make_shared<Outputs>();
        shared_ptr<exception> storedException;
        shared_ptr<future<void>> futureHolder = make_shared<future<void>>();
        *futureHolder = do_thread_async(
            [thisHolder, this, schemeHeight, ids, result, storedException]()
            {
                try
                {
                    *result = GenerateOutputsSync(schemeHeight, ids);
                }
                catch (const exception& ex)
                {
                    *storedException = ex;
                }
            },
            [futureHolder, resultCallback = move(resultCallback), exceptionCallback = move(exceptionCallback), result, storedException]() mutable
            {
                if (storedException)
                {
                    exceptionCallback(*storedException);
                }
                else
                {
                    resultCallback(move(*result));
                }
                futureHolder.reset();
            });
        
    }

    size_t LocalPrivateKeyKeeper::AllocateNonceSlot()
    {
		++m_NonceSlotLast %= kMaxNonces;

		if (m_NonceSlotLast >= m_Nonces.size())
		{
			m_NonceSlotLast = m_Nonces.size();
			m_Nonces.emplace_back();
		}

        // Don't store the generated nonce for the kernel multisig. Instead - store the raw random, from which the nonce is derived using kdf.

        ECC::GenRandom(m_Nonces[m_NonceSlotLast].V);

        SaveNonceSeeds();

        return m_NonceSlotLast;
    }

    ////

    IPrivateKeyKeeper::PublicKeys LocalPrivateKeyKeeper::GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey)
    {
        PublicKeys result;
        Scalar::Native secretKey;
        result.reserve(ids.size());
        if (createCoinKey)
        {
            for (const auto& coinID : ids)
            {
                Point& publicKey = result.emplace_back();
                SwitchCommitment().Create(secretKey, publicKey, *GetChildKdf(coinID), coinID);
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

    ECC::Point LocalPrivateKeyKeeper::GeneratePublicKeySync(const Key::IDV& id, bool createCoinKey)
    {
        Scalar::Native secretKey;
        Point publicKey;

        if (createCoinKey)
        {
            SwitchCommitment().Create(secretKey, publicKey, *GetChildKdf(id), id);
        }
        else
        {
            m_MasterKdf->DeriveKey(secretKey, id);
            publicKey = Context::get().G * secretKey;
        }
        return publicKey;
    }

    IPrivateKeyKeeper::Outputs LocalPrivateKeyKeeper::GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids)
    {
        Outputs result;
        Scalar::Native secretKey;
        Point commitment;
        result.reserve(ids.size());
        for (const auto& coinID : ids)
        {
            auto& output = result.emplace_back(make_unique<Output>());
            output->Create(schemeHeigh, secretKey, *GetChildKdf(coinID), coinID, *m_MasterKdf);
        }
        return result;
    }

    ECC::Point LocalPrivateKeyKeeper::GenerateNonceSync(size_t slot)
    {
        Point::Native result = Context::get().G * GetNonce(slot);
        return result;
    }

    Scalar LocalPrivateKeyKeeper::SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const Scalar::Native& offset, size_t nonceSlot, const ECC::Hash::Value& message, const Point::Native& publicNonce, const Point::Native& commitment)
    {
        auto excess = GetExcess(inputs, outputs, offset);

        ECC::Signature::MultiSig multiSig;
        ECC::Scalar::Native partialSignature;
        multiSig.m_NoncePub = publicNonce;
        multiSig.m_Nonce = GetNonce(nonceSlot);
        multiSig.SignPartial(partialSignature, message, excess);
    
        return Scalar(partialSignature);
    }

    void LocalPrivateKeyKeeper::LoadNonceSeeds()
    {
        try
        {
			ByteBuffer buffer;
			if (m_WalletDB->getBlob(LOCAL_NONCE_SEEDS, buffer) && !buffer.empty())
            {
                Deserializer d;
                d.reset(buffer);
                d & m_Nonces;
				d & m_NonceSlotLast;
            }
        }
        catch (...)
        {
			m_Nonces.clear();
        }

		if (m_NonceSlotLast >= m_Nonces.size())
			m_NonceSlotLast = m_Nonces.size() - 1;
    }

    void LocalPrivateKeyKeeper::SaveNonceSeeds()
    {
        Serializer s;
        s & m_Nonces;
		s & m_NonceSlotLast;
        ByteBuffer buffer;
        s.swap_buf(buffer);
        m_WalletDB->setVarRaw(LOCAL_NONCE_SEEDS, buffer.data(), buffer.size());
    }

    ////

	Key::IKdf::Ptr LocalPrivateKeyKeeper::GetChildKdf(const Key::IDV& kidv) const
	{
		return MasterKey::get_Child(m_MasterKdf, kidv);
    }

    Scalar::Native LocalPrivateKeyKeeper::GetNonce(size_t slot)
    {
        const auto& randomValue = m_Nonces[slot].V;

        NoLeak<Scalar::Native> nonce;
        m_MasterKdf->DeriveKey(nonce.V, randomValue);
        return nonce.V;
    }

    Scalar::Native LocalPrivateKeyKeeper::GetExcess(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const ECC::Scalar::Native& offset) const
    {
        // Excess = Sum(input blinfing factors) - Sum(output blinfing factors) - offset
        Point commitment;
        Scalar::Native blindingFactor;
        Scalar::Native excess = offset;
        for (const auto& coinID : outputs)
        {
            SwitchCommitment().Create(blindingFactor, commitment, *GetChildKdf(coinID), coinID);
            excess += blindingFactor;
        }
        excess = -excess;

        for (const auto& coinID : inputs)
        {
            SwitchCommitment().Create(blindingFactor, commitment, *GetChildKdf(coinID), coinID);
            excess += blindingFactor;
        }
        return excess;
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
            m_EventToUpdate = io::AsyncEvent::create(io::Reactor::get_Current(), [this, weak = this->weak_from_this()]()
            { 
                if (auto l = weak.lock())
                {
                    Update();
                }
            });
            m_EventToUpdate->post();
        }
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
            m_EventToUpdate.reset();
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
            OnFailed(TxFailureReason::Unknown);
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
            SetParameter(TxParameterID::KernelProofHeight, Height(0));
            SetParameter(TxParameterID::KernelUnconfirmedHeight, Height(0));
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

    IAsyncContext& BaseTransaction::GetAsyncAcontext() const
    {
        return m_Gateway;
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
}