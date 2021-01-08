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
#include "common.h"
#include "wallet_db.h"

#include <boost/optional.hpp>

#include "private_key_keeper.h"

#include <memory>

namespace beam::wallet
{
    TxID GenerateTxID();
    TxParameters CreateTransactionParameters(TxType type, const boost::optional<TxID>& oTxId = boost::optional<TxID>());
    //
    // Interface for all possible transaction types in active state
    //
    struct ITransaction
    {
        using Ptr = std::shared_ptr<ITransaction>;

        // Type of transaction
        virtual TxType GetType() const = 0;

        // Updates state of transation. 
        virtual void Update() = 0;

        virtual bool CanCancel() const = 0;

        // Cancel active transaction
        virtual void Cancel() = 0;

        // Rollback transation state up to give height
        virtual bool Rollback(Height height) = 0;

        // Returns true if negotiation is finished and all needed data is sent
        virtual bool IsInSafety() const = 0;
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

        class TxContext
        {
        public:
            TxContext(INegotiatorGateway& gateway, IWalletDB::Ptr db, const TxID& txID, SubTxID subTxID = kDefaultSubTxID)
                : m_Gateway(gateway)
                , m_WalletDB(db)
                , m_TxID(txID)
                , m_SubTxID(subTxID)
            {

            }

            TxContext(const TxContext& parentContext, INegotiatorGateway& gateway, SubTxID subTxID = kDefaultSubTxID)
                : TxContext(gateway, parentContext.m_WalletDB, parentContext.m_TxID, (subTxID == kDefaultSubTxID) ? parentContext.m_SubTxID : subTxID)
            {

            }

            INegotiatorGateway& GetGateway() const
            {
                return m_Gateway;
            }

            IWalletDB::Ptr GetWalletDB() const
            {
                return m_WalletDB;
            }

            const TxID& GetTxID() const
            {
                return m_TxID;
            }

            SubTxID GetSubTxID() const
            {
                return m_SubTxID;
            }
        private:
            INegotiatorGateway& m_Gateway;
            IWalletDB::Ptr m_WalletDB;
            TxID m_TxID;
            SubTxID m_SubTxID;
        };

        class Creator
        {
        public:
            using Ptr = std::shared_ptr<Creator>;

            virtual ~Creator() = default;

            // Creates new instance of transaction (virtual constructor)
            virtual BaseTransaction::Ptr Create(const TxContext& context) = 0;

            // Allows to add any additional user's checks and enhancements of parameters. Should throw exceptions if something is wrong
            virtual TxParameters CheckAndCompleteParameters(const TxParameters& p) { return p; } // TODO: find better solution without redundant copies
        };

        BaseTransaction(const TxType type, const TxContext& context);
        virtual ~BaseTransaction() = default;

        const TxID& GetTxID() const;
        void Update() override;
        bool CanCancel() const override;
        void Cancel() override;

        bool Rollback(Height height) override;

        static const uint32_t s_ProtoVersion;

        virtual bool IsTxParameterExternalSettable(TxParameterID paramID, SubTxID subTxID) const
        {
            return false;
        }

        template <typename T>
        bool GetParameter(TxParameterID paramID, T& value, SubTxID subTxID) const
        {
            return storage::getTxParameter(*GetWalletDB(), GetTxID(), subTxID, paramID, value);
        }

        template <typename T>
        bool GetParameter(TxParameterID paramID, T& value) const
        {
            return GetParameter(paramID, value, m_Context.GetSubTxID());
        }

        template <typename T>
        T GetMandatoryParameter(TxParameterID paramID, SubTxID subTxID) const
        {
            T value{};

            if (!GetParameter(paramID, value, subTxID))
            {
                LogFailedParameter(paramID, subTxID);
                throw TransactionFailedException(true, TxFailureReason::FailedToGetParameter);
            }
            return value;
        }

        template <typename T>
        T GetMandatoryParameter(TxParameterID paramID) const
        {
            return GetMandatoryParameter<T>(paramID, m_Context.GetSubTxID());
        }

        template <typename T>
        bool SetParameter(TxParameterID paramID, const T& value, SubTxID subTxID)
        {
            return SetParameter(paramID, value, true, subTxID);
        }

        template <typename T>
        bool SetParameter(TxParameterID paramID, const T& value)
        {
            return SetParameter(paramID, value, m_Context.GetSubTxID());
        }

        template <typename T>
        bool SetParameter(TxParameterID paramID, const T& value, bool shouldNotifyAboutChanges, SubTxID subTxID)
        {
            return storage::setTxParameter(*GetWalletDB(), GetTxID(), subTxID, paramID, value, shouldNotifyAboutChanges);
        }

        template <typename T>
        bool SetParameter(TxParameterID paramID, const T& value, bool shouldNotifyAboutChanges)
        {
            return SetParameter(paramID, value, shouldNotifyAboutChanges, m_Context.GetSubTxID());
        }

        template <typename T>
        void CopyParameter(TxParameterID paramID, BaseTransaction& tx)
        {
            T value;
            if (GetParameter(paramID, value))
            {
                tx.SetParameter(paramID, value);
            }
        }

        template <typename T>
        void SetState(T state, SubTxID subTxID)
        {
            SetParameter(TxParameterID::State, state, true, subTxID);
        }

        template <typename T>
        T GetState(SubTxID subTxId) const
        {
            static_assert(std::is_enum<T>::value, "State must be an enum");

            T state = T(0);
            GetParameter(TxParameterID::State, state, subTxId);
            return state;
        }

        template <typename T>
        T GetState() const
        {
            return GetState<T>(m_Context.GetSubTxID());
        }

        template <typename T>
        void SetState(T state)
        {
            SetParameter(TxParameterID::State, state, true, m_Context.GetSubTxID());
        }

        TxType GetType() const override
        {
            return m_txType;
        }

        IWalletDB::Ptr GetWalletDB() const;
        IPrivateKeyKeeper2::Ptr get_KeyKeeperStrict(); // throws TxFailureReason::NoKeyKeeper if no key keeper (read-only mode)
        Key::IKdf::Ptr get_MasterKdfStrict() const; // throws TxFailureReason::NoMasterKey if no master key
        static void TestKeyKeeperRet(IPrivateKeyKeeper2::Status::Type); // throws TxFailureReason::KeyKeeperError on error
        static TxFailureReason KeyKeeperErrorToFailureReason(IPrivateKeyKeeper2::Status::Type);
        IAsyncContext& GetAsyncAcontext() const;
        bool IsInitiator() const;
        uint32_t get_PeerVersion() const;
        bool GetTip(Block::SystemState::Full& state) const;
        void UpdateAsync();
        void UpdateOnNextTip();
        INegotiatorGateway& GetGateway() const;
        SubTxID GetSubTxID() const;

        IPrivateKeyKeeper2::Slot::Type GetSlotSafe(bool bAllocateIfAbsent);
        void FreeSlotSafe();

        virtual void FreeResources();
        virtual void OnFailed(TxFailureReason reason, bool notify = false);

        void SendTxParametersStrict(SetTxParameter&& msg) const
        {
            if (!SendTxParameters(std::move(msg)))
                throw TransactionFailedException(false, TxFailureReason::FailedToSendParameters);
        }

    protected:
        //
        // Assets support stuff. Since it is needed in many places,
        // moved from SimpleTx to the base class
        //
        enum AssetCheckState {
            ACInitial,
            ACConfirmation,
            ACCheck,
        };

        enum AssetCheckResult {
            Fail,
            Async,
            OK,
        };

        AssetCheckResult CheckAsset(Asset::ID);
        AssetCheckState m_assetCheckState = AssetCheckState::ACInitial;

    protected:
        virtual bool CheckExpired();
        virtual bool CheckExternalFailures();
        void ConfirmKernel(const Merkle::Hash& kernelID);
        void CompleteTx();
        virtual void RollbackTx();
        virtual void NotifyFailure(TxFailureReason);
        void UpdateTxDescription(TxStatus s);
        bool IsSelfTx() const;

        bool SendTxParameters(SetTxParameter&& msg) const;
        virtual void UpdateImpl() = 0;

        void SetCompletedTxCoinStatuses(Height proofHeight);
        void LogFailedParameter(TxParameterID paramID, SubTxID subTxID) const;

    protected:
        TxType m_txType;
        TxContext m_Context;
        mutable boost::optional<bool> m_IsInitiator;
        io::AsyncEvent::Ptr m_EventToUpdate;
    };
}

namespace beam
{
    std::ostream& operator<<(std::ostream& os, const wallet::BaseTransaction::TxContext& context);
}
