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

#include "push_transaction.h"

namespace beam::wallet::lelantus
{
    BaseTransaction::Ptr PushTransaction::Creator::Create(INegotiatorGateway& gateway
        , IWalletDB::Ptr walletDB
        , IPrivateKeyKeeper::Ptr keyKeeper
        , const TxID& txID)
    {
        return BaseTransaction::Ptr(new PushTransaction(gateway, walletDB, keyKeeper, txID));
    }

    TxParameters PushTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        // TODO roman.strilets implement this
        return parameters;
    }

    PushTransaction::PushTransaction(INegotiatorGateway& gateway
        , IWalletDB::Ptr walletDB
        , IPrivateKeyKeeper::Ptr keyKeeper
        , const TxID& txID)
        : BaseTransaction(gateway, walletDB, keyKeeper, txID)
    {
    }

    TxType PushTransaction::GetType() const
    {
        return TxType::PushTransaction;
    }

    bool PushTransaction::IsInSafety() const
    {
        // TODO roman.strilets implement this
        return true;
    }

    void PushTransaction::UpdateImpl()
    {

    }
}