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

#include "common.h"
#include "wallet_db.h"
#include "base_transaction.h"

#include <boost/optional.hpp>

namespace beam::wallet
{
    TxParameters CreateSimpleTransactionParameters(const boost::optional<TxID>& txId = boost::none);
    TxParameters CreateSplitTransactionParameters(const AmountList& amountList, const boost::optional<TxID>& txId = boost::none);

    class SimpleTxBuilder;

    class SimpleTransaction : public BaseTransaction
    {
    public:
        enum State : uint8_t
        {
            Initial,
            Invitation,
            PeerConfirmation,
            InvitationConfirmation,
            Registration,
            KernelConfirmation,
            OutputsConfirmation,
        };

        class Creator : public BaseTransaction::Creator
        {
        public:
            explicit Creator(IWalletDB::Ptr walletDB);

        private:
            BaseTransaction::Ptr Create(const TxContext& context) override;
            TxParameters CheckAndCompleteParameters(const TxParameters& parameters) override;

        private:
            IWalletDB::Ptr m_WalletDB;
        };

    private:
        explicit SimpleTransaction(const TxContext& context);

        bool IsSelfTx() const;
        bool IsInSafety() const override;
        void UpdateImpl() override;
        bool IsTxParameterExternalSettable(TxParameterID paramID, SubTxID subTxID) const override;

    private:
        struct MyBuilder;
        std::shared_ptr<SimpleTxBuilder> m_TxBuilder;
    };
}
