// Copyright 2020 The Beam Team
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

#include "wallet/transactions/swaps/second_side.h"
#include "wallet/transactions/swaps/common.h"
#include "wallet/core/base_transaction.h"
//#include "bridge.h"
//#include "settings_provider.h"

#include <memory>

namespace beam::wallet
{
    class EthereumSide : public SecondSide, public std::enable_shared_from_this<EthereumSide>
    {
    public:
        EthereumSide(BaseTransaction& tx);
        virtual ~EthereumSide();

        bool Initialize() override;
        bool InitLockTime() override;
        bool ValidateLockTime() override;
        void AddTxDetails(SetTxParameter& txParameters) override;
        bool ConfirmLockTx() override;
        bool ConfirmRefundTx() override;
        bool ConfirmRedeemTx() override;
        bool SendLockTx() override;
        bool SendRefund() override;
        bool SendRedeem() override;
        bool IsLockTimeExpired() override;
        bool HasEnoughTimeToProcessLockTx() override;
        bool IsQuickRefundAvailable() override;
    };
} // namespace beam::wallet