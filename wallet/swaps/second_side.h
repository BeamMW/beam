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

#include <memory>

namespace beam::wallet
{
    struct SetTxParameter;

    class SecondSide
    {
    public:
        using Ptr = std::shared_ptr<SecondSide>;
        
        virtual ~SecondSide() = default;

        virtual bool Initialize() = 0;
        virtual bool InitLockTime() = 0;
        virtual bool ValidateLockTime() = 0;
        virtual void AddTxDetails(SetTxParameter&) = 0;
        virtual bool ConfirmLockTx() = 0;
        virtual bool ConfirmRefundTx() = 0;
        virtual bool ConfirmRedeemTx() = 0;
        virtual bool SendLockTx() = 0;
        virtual bool SendRefund() = 0;
        virtual bool SendRedeem() = 0;
        virtual bool IsLockTimeExpired() = 0;
        virtual bool HasEnoughTimeToProcessLockTx() = 0;
    };
}