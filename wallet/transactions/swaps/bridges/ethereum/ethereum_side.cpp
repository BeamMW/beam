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

#include "ethereum_side.h"

namespace beam::wallet
{
EthereumSide::EthereumSide(BaseTransaction& /*tx*/)
{

}

EthereumSide::~EthereumSide()
{}

bool EthereumSide::Initialize()
{
    return false;
}

bool EthereumSide::InitLockTime()
{
    return false;
}

bool EthereumSide::ValidateLockTime()
{
    return false;
}

void EthereumSide::AddTxDetails(SetTxParameter& /*txParameters*/)
{}

bool EthereumSide::ConfirmLockTx()
{
    return false;
}

bool EthereumSide::ConfirmRefundTx()
{
    return false;
}

bool EthereumSide::ConfirmRedeemTx()
{
    return false;
}

bool EthereumSide::SendLockTx()
{
    return false;
}

bool EthereumSide::SendRefund()
{
    return false;
}

bool EthereumSide::SendRedeem()
{
    return false;
}

bool EthereumSide::IsLockTimeExpired()
{
    return false;
}

bool EthereumSide::HasEnoughTimeToProcessLockTx()
{
    return false;
}

bool EthereumSide::IsQuickRefundAvailable()
{
    return false;
}
} // namespace beam::wallet