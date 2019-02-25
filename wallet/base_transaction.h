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

namespace beam::wallet
{
    TxID GenerateTxID();

    struct ITransaction
    {
        using Ptr = std::shared_ptr<ITransaction>;
        virtual TxType GetType() const = 0;
        virtual void Update() = 0;
        virtual void Cancel() = 0;
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
    {
    public:
        using Ptr = std::shared_ptr<BaseTransaction>;
        BaseTransaction(INegotiatorGateway& gateway
            , beam::IWalletDB::Ptr walletDB
            , const TxID& txID);
        virtual ~BaseTransaction() {}

        const TxID& GetTxID() const;
        void Update() override;
        void Cancel() override;

        static const uint32_t s_ProtoVersion;

        template <typename T>
        bool GetParameter(TxParameterID paramID, T& value, SubTxID subTxID = kDefaultSubTxID) const
        {
            return getTxParameter(*m_WalletDB, GetTxID(), subTxID, paramID, value);
        }

        template <typename T>
        T GetMandatoryParameter(TxParameterID paramID, SubTxID subTxID = kDefaultSubTxID) const
        {
            T value{};
            if (!getTxParameter(*m_WalletDB, GetTxID(), subTxID, paramID, value))
            {
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
            return setTxParameter(*m_WalletDB, GetTxID(), subTxID, paramID, value, shouldNotifyAboutChanges);
        }

        template <typename T>
        void SetState(T state, SubTxID subTxID = kDefaultSubTxID)
        {
            SetParameter(TxParameterID::State, state, true, subTxID);
        }

        IWalletDB::Ptr GetWalletDB();
        bool IsInitiator() const;
        uint32_t get_PeerVersion() const;

    protected:
        bool CheckExpired();
        bool CheckExternalFailures();
        void ConfirmKernel(const TxKernel& kernel);
        void CompleteTx();
        void RollbackTx();
        void NotifyFailure(TxFailureReason);
        void UpdateTxDescription(TxStatus s);

        void OnFailed(TxFailureReason reason, bool notify = false);

        bool GetTip(Block::SystemState::Full& state) const;

        bool SendTxParameters(SetTxParameter&& msg) const;
        virtual void UpdateImpl() = 0;

        virtual bool ShouldNotifyAboutChanges(TxParameterID paramID) const { return true; };
    protected:

        INegotiatorGateway& m_Gateway;
        beam::IWalletDB::Ptr m_WalletDB;

        TxID m_ID;
        mutable boost::optional<bool> m_IsInitiator;
    };
}