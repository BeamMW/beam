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

#include "wallet/core/common.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/base_transaction.h"
#include "wallet/core/base_tx_builder.h"

namespace beam::wallet
{
    TxParameters CreateDexTransactionParams(
            const DexOrderID& dexOrderID,
            const WalletID& peerID,
            const WalletID& myID,
            Asset::ID coinMy,
            Amount amountMy,
            Asset::ID coinPeer,
            Amount amountPeer,
            Amount fee,
            const boost::optional<TxID>& txId = boost::none
    );

    class DexSimpleSwapBuilder;
    class DexTransaction
        : public BaseTransaction
    {
    public:
        class Creator
            : public BaseTransaction::Creator
        {
        public:
            explicit Creator(IWalletDB::Ptr);

        private:
            BaseTransaction::Ptr Create(const TxContext& context) override;
            TxParameters CheckAndCompleteParameters(const TxParameters& parameters) override;

        private:
            IWalletDB::Ptr _wdb;
        };

    private:
        explicit DexTransaction(const TxContext& context);

        enum class State : uint8_t
        {
            Initial,
            Registration,
            KernelConfirmation,
        };

        bool IsInSafety() const override;
        bool IsTxParameterExternalSettable(TxParameterID paramID, SubTxID subTxID) const override;
        void UpdateImpl() override;

    private:
        std::shared_ptr<DexSimpleSwapBuilder> _builder;
    };
}
