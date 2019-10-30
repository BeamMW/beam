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

#pragma once

#include "wallet/common.h"
#include "wallet/wallet_db.h"
#include "base_transaction.h"

#include <condition_variable>
#include <boost/optional.hpp>
#include "utility/logger.h"

namespace beam::wallet
{
    class BaseTxBuilder;

    TxParameters CreateSimpleTransactionParameters(boost::optional<TxID> txId = boost::none);
    TxParameters CreateSplitTransactionParameters(const WalletID& myID, const AmountList& amountList, boost::optional<TxID> txId = boost::none);

    class SimpleTransaction : public BaseTransaction
    {
        enum State : uint8_t
        {
            Initial,
            Invitation,
            PeerConfirmation,
            
            InvitationConfirmation,
            Registration,

            KernelConfirmation,
            OutputsConfirmation
        };
    public:
        class Creator : public BaseTransaction::Creator
        {
        public:
            Creator(IWalletDB::Ptr walletDB);
        private:
            BaseTransaction::Ptr Create(INegotiatorGateway& gateway
                                      , IWalletDB::Ptr walletDB
                                      , IPrivateKeyKeeper::Ptr keyKeeper
                                      , const TxID& txID) override;
            TxParameters CheckAndCompleteParameters(const TxParameters& parameters) override;
        private:
            IWalletDB::Ptr m_WalletDB;
        };
    private:
        SimpleTransaction(INegotiatorGateway& gateway
                        , IWalletDB::Ptr walletDB
                        , IPrivateKeyKeeper::Ptr keyKeeper
                        , const TxID& txID);
    private:
        TxType GetType() const override;
        void UpdateImpl() override;
        bool ShouldNotifyAboutChanges(TxParameterID paramID) const override;
        void SendInvitation(const BaseTxBuilder& builder, bool isSender);
        void ConfirmInvitation(const BaseTxBuilder& builder, bool sendUtxos);
        void ConfirmTransaction(const BaseTxBuilder& builder, bool sendUtxos);
        void NotifyTransactionRegistered();
        bool IsSelfTx() const;
        State GetState() const;
    private:
        std::shared_ptr<BaseTxBuilder> m_TxBuilder;
    };
}
