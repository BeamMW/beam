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

#pragma once

#include "wallet/common.h"
#include "wallet/wallet_db.h"

#include <condition_variable>
#include <boost/optional.hpp>
#include "utility/logger.h"

#include <memory>

#if defined(BEAM_HW_WALLET)
#include "hw_wallet.h"
#endif

namespace beam::wallet
{
    TxID GenerateTxID();

    //
    // Interface for all possible transaction types
    //
    struct ITransaction
    {
        using Ptr = std::shared_ptr<ITransaction>;
        virtual TxType GetType() const = 0;
        virtual void Update() = 0;
        virtual void Cancel() = 0;
        virtual bool Rollback(Height height) = 0;
    };



    //
    // Interface to master key storage. HW wallet etc.
    // Only public info should cross its boundary.
    //
    struct IPrivateKeyKeeper
    {
        using Ptr = std::shared_ptr<IPrivateKeyKeeper>;

        template<typename R>
        using Callback = std::function<void(R&&)>;
        using ExceptionCallback = Callback<const std::exception&>;
        using PublicKeys = std::vector<ECC::Point>;
        using RangeProofs = std::vector<std::unique_ptr<ECC::RangeProof::Confidential>>;
        using Outputs = std::vector<Output::Ptr>;

        struct Nonce
        {
            uint8_t m_Slot = 0;
            ECC::Point m_PublicValue;
        };

        virtual void GeneratePublicKeys(const std::vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&&, ExceptionCallback&&) = 0;
        virtual void GenerateOutputs(Height schemeHeigh, const std::vector<Key::IDV>& ids, Callback<Outputs>&&, ExceptionCallback&&) = 0;

        virtual size_t AllocateNonceSlot() = 0;

        // sync part for integration test
        virtual PublicKeys GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey) = 0;
        virtual ECC::Point GeneratePublicKeySync(const Key::IDV& id, bool createCoinKey) = 0;
        virtual Outputs GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids) = 0;
        //virtual RangeProofs GenerateRangeProofSync(Height schemeHeigh, const std::vector<Key::IDV>& ids) = 0;
        virtual ECC::Point GenerateNonceSync(size_t slot) = 0;
        virtual ECC::Scalar SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const ECC::Scalar::Native& offset, size_t nonceSlot, const ECC::Hash::Value& message, const ECC::Point::Native& publicNonce, const ECC::Point::Native& commitment) = 0;
    };

#if defined(BEAM_HW_WALLET)
    class TrezorKeyKeeper : public IPrivateKeyKeeper
        , public std::enable_shared_from_this<TrezorKeyKeeper>
    {
    public:
        TrezorKeyKeeper()
            //: m_latestSlot(0)
        {

        }
    private:
        void GeneratePublicKeys(const std::vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback) override
        {

        }

        void GenerateOutputs(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback) override
        {
            using namespace std;

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

        size_t AllocateNonceSlot() override
        {
            m_latestSlot++;
            m_hwWallet.generateNonceSync((uint8_t)m_latestSlot);
            return m_latestSlot;
        }

        PublicKeys GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey) override
        {
            PublicKeys result;
            result.reserve(ids.size());

            for (const auto& idv : ids)
            {
                ECC::Point& publicKey = result.emplace_back();
                publicKey = m_hwWallet.generateKeySync(idv, createCoinKey);
            }

            return result;
        }

        ECC::Point GeneratePublicKeySync(const Key::IDV& id, bool createCoinKey) override
        {
            return m_hwWallet.generateKeySync(id, createCoinKey);
        }

        Outputs GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids) override
        {
            Outputs outputs;
            outputs.reserve(ids.size());

            for (const auto& kidv : ids)
            {
                auto& output = outputs.emplace_back(std::make_unique<Output>());
                output->m_Commitment = m_hwWallet.generateKeySync(kidv, false);

                output->m_pConfidential.reset(new ECC::RangeProof::Confidential);
                *output->m_pConfidential = m_hwWallet.generateRangeProofSync(kidv, false);
            }

            return outputs;
        }

        ECC::Point GenerateNonceSync(size_t slot) override
        {
            assert(m_latestSlot >= slot);
            return m_hwWallet.getNoncePublicSync((uint8_t)slot);
        }

        ECC::Scalar SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const ECC::Scalar::Native& offset, size_t nonceSlot, const ECC::Hash::Value& message, const ECC::Point::Native& publicNonce, const ECC::Point::Native& commitment) override
        {
            HWWallet::TxData txData;
            txData.fee = 2;
            txData.height = {0, 865};
            txData.kernelCommitment = commitment;
            txData.kernelNonce = publicNonce;
            txData.nonceSlot = (uint32_t)nonceSlot;
            txData.offset = offset;

            return m_hwWallet.signTransactionSync(inputs, outputs, txData);
        }
    private:
        beam::HWWallet m_hwWallet;

        // TODO: temporary made it static to pass two wallets negotiations test
        static size_t m_latestSlot;
    };
#endif

    //
    // Private key keeper in local storage implementation
    //
    class LocalPrivateKeyKeeper : public IPrivateKeyKeeper
                                , public std::enable_shared_from_this<LocalPrivateKeyKeeper>
    {
    public:
        LocalPrivateKeyKeeper(IWalletDB::Ptr walletDB);
    private:
        void GeneratePublicKeys(const std::vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback) override;
        void GenerateOutputs(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<Outputs>&&, ExceptionCallback&&) override;

        size_t AllocateNonceSlot() override;

        PublicKeys GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey) override;
        ECC::Point GeneratePublicKeySync(const Key::IDV& id, bool createCoinKey) override;
        Outputs GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids) override;
        //RangeProofs GenerateRangeProofSync(Height schemeHeight, const std::vector<Key::IDV>& ids) override;
        ECC::Point GenerateNonceSync(size_t slot) override;
        ECC::Scalar SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const ECC::Scalar::Native& offset, size_t nonceSlot, const ECC::Hash::Value& message, const ECC::Point::Native& publicNonce, const ECC::Point::Native& commitment) override;

    private:
        ECC::uintBig GetSeedKid(Key::IPKdf& tagKdf, const ECC::Point& commitment) const;
        Key::IKdf::Ptr GetChildKdf(Key::Index iKdf) const;
        ECC::Scalar::Native GetNonce(size_t slot);
        ECC::Scalar::Native GetExcess(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const ECC::Scalar::Native& offset) const;
        void LoadNonceSeeds();
        void SaveNonceSeeds();
    private:
        IWalletDB::Ptr m_WalletDB;
        Key::IKdf::Ptr m_MasterKdf;

        std::map<size_t, ECC::Hash::Value> m_Nonces;
    };

    std::string GetFailureMessage(TxFailureReason reason);

    class TransactionFailedException : public std::runtime_error
    {
    public:
        TransactionFailedException(bool notify, TxFailureReason reason, const char* message = "");
        bool ShouldNofify() const;
        TxFailureReason GetReason() const;
    private:
        bool m_Notify;
        TxFailureReason m_Reason;
    };


    //
    // State machine for managing per transaction negotiations between wallets
    // 
    class BaseTransaction : public ITransaction
                          , public std::enable_shared_from_this<ITransaction>
    {
    public:
        using Ptr = std::shared_ptr<BaseTransaction>;
        BaseTransaction(INegotiatorGateway& gateway
                      , IWalletDB::Ptr walletDB
                      , IPrivateKeyKeeper::Ptr keyKeeper
                      , const TxID& txID);
        virtual ~BaseTransaction(){}

        const TxID& GetTxID() const;
        void Update() override;
        void Cancel() override;

        bool Rollback(Height height) override;

        static const uint32_t s_ProtoVersion;

        template <typename T>
        bool GetParameter(TxParameterID paramID, T& value, SubTxID subTxID = kDefaultSubTxID) const
        {
            return storage::getTxParameter(*m_WalletDB, GetTxID(), subTxID, paramID, value);
        }

        template <typename T>
        T GetMandatoryParameter(TxParameterID paramID, SubTxID subTxID = kDefaultSubTxID) const
        {
            T value{};
            if (!storage::getTxParameter(*m_WalletDB, GetTxID(), subTxID, paramID, value))
            {
                LOG_ERROR() << GetTxID() << " Failed to get parameter: " << (int)paramID;
                throw TransactionFailedException(true, TxFailureReason::FailedToGetParameter);
            }
            return value;
        }

        template <typename T>
        bool SetParameter(TxParameterID paramID, const T& value, SubTxID subTxID = kDefaultSubTxID)
        {
            bool shouldNotifyAboutChanges = ShouldNotifyAboutChanges(paramID);
            return SetParameter(paramID, value, shouldNotifyAboutChanges, subTxID);
        }

        template <typename T>
        bool SetParameter(TxParameterID paramID, const T& value, bool shouldNotifyAboutChanges, SubTxID subTxID = kDefaultSubTxID)
        {
            return storage::setTxParameter(*m_WalletDB, GetTxID(), subTxID, paramID, value, shouldNotifyAboutChanges);
        }

        template <typename T>
        void SetState(T state, SubTxID subTxID = kDefaultSubTxID)
        {
            SetParameter(TxParameterID::State, state, true, subTxID);
        }

        IWalletDB::Ptr GetWalletDB();
        IPrivateKeyKeeper::Ptr GetKeyKeeper();
        IAsyncContext& GetAsyncAcontext() const;
        bool IsInitiator() const;
        uint32_t get_PeerVersion() const;
        bool GetTip(Block::SystemState::Full& state) const;
        void UpdateAsync();
    protected:
        virtual bool CheckExpired();
        virtual bool CheckExternalFailures();
        void ConfirmKernel(const Merkle::Hash& kernelID);
        void UpdateOnNextTip();
        void CompleteTx();
        virtual void RollbackTx();
        virtual void NotifyFailure(TxFailureReason);
        void UpdateTxDescription(TxStatus s);

        virtual void OnFailed(TxFailureReason reason, bool notify = false);

        bool SendTxParameters(SetTxParameter&& msg) const;
        virtual void UpdateImpl() = 0;

        virtual bool ShouldNotifyAboutChanges(TxParameterID paramID) const { return true; };
    protected:

        INegotiatorGateway& m_Gateway;
        IWalletDB::Ptr m_WalletDB;
        IPrivateKeyKeeper::Ptr m_KeyKeeper;

        TxID m_ID;
        mutable boost::optional<bool> m_IsInitiator;
        io::AsyncEvent::Ptr m_EventToUpdate;
    };
}