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

#include <condition_variable>
#include <boost/optional.hpp>
#include "utility/logger.h"
#include "aregister_tx_builder.h"

namespace beam::wallet
{
    class BaseTxBuilder;

    class AssetRegisterTransaction : public BaseTransaction
    {
    public:
        class Creator : public BaseTransaction::Creator
        {
        public:
            Creator() = default;
        private:
            BaseTransaction::Ptr Create(INegotiatorGateway& gateway, IWalletDB::Ptr walletDB, IPrivateKeyKeeper::Ptr keyKeeper, const TxID& txID) override;
            TxParameters CheckAndCompleteParameters(const TxParameters& p) override;
        };

    private:
        AssetRegisterTransaction(INegotiatorGateway& gateway, IWalletDB::Ptr walletDB, IPrivateKeyKeeper::Ptr keyKeeper, const TxID& txID);
        TxType GetType() const override;
        bool IsInSafety() const override;

        void UpdateImpl() override;
        bool ShouldNotifyAboutChanges(TxParameterID paramID) const override;
        bool IsLoopbackTransaction() const;
        AssetRegisterTxBuilder& GetTxBuilder();

        enum State : uint8_t
        {
            Initial,
            MakingInputs,
            MakingOutputs,
            MakingKernels,
            Registration,
            KernelConfirmation,
            AssetConfirmation
        };
        State GetState() const;

    private:
        std::shared_ptr<AssetRegisterTxBuilder> _builder;
    };
}
