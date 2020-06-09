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

#include "unlink_transaction.h"
#include "pull_transaction.h"
#include "push_transaction.h"
#include "core/shielded.h"
#include "push_tx_builder.h"
#include "wallet/core/strings_resources.h"

namespace beam::wallet::lelantus
{
    TxParameters CreateUnlinkFundsTransactionParameters(const WalletID& myID, const boost::optional<TxID>& txId)
    {
        return CreateTransactionParameters(TxType::UnlinkFunds, txId)
            .SetParameter(TxParameterID::MyID, myID);
    }

    BaseTransaction::Ptr UnlinkFundsTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new UnlinkFundsTransaction(context, m_withAssets));
    }

    TxParameters UnlinkFundsTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        // TODO roman.strilets implement this
        return parameters;
    }

    UnlinkFundsTransaction::UnlinkFundsTransaction(const TxContext& context
        , bool withAssets)
        : BaseTransaction(context)
        , m_withAssets(withAssets)
    {
       // m_ActiveTransactions.push_back(static_cast<BaseTransaction::Creator&>(PushTransaction::Creator(m_withAssets)).Create(GetGateway(), m_WalletDB, GetTxID()));
    }

    TxType UnlinkFundsTransaction::GetType() const
    {
        return TxType::UnlinkFunds;
    }

    bool UnlinkFundsTransaction::IsInSafety() const
    {
        // TODO roman.strilets implement this
        return true;
    }

    void UnlinkFundsTransaction::UpdateImpl()
    {
        auto state = GetState();
        switch(state)
        {
        case State::Initial:
            CreateInitialTransactions();
            UpdateActiveTransactions();
            SetState(State::Unlinking);
            break;
        case State::Unlinking:
            //ResumeActiveTransactions();
            UpdateActiveTransactions();
            break;
        
        }
    }
    
    void UnlinkFundsTransaction::RollbackTx()
    {
        LOG_INFO() << GetTxID() << " Transaction failed. Rollback...";
        GetWalletDB()->rollbackTx(GetTxID());
        GetWalletDB()->deleteShieldedCoinsCreatedByTx(GetTxID());
    }

    UnlinkFundsTransaction::State UnlinkFundsTransaction::GetState() const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state);
        return state;
    }

    void UnlinkFundsTransaction::UpdateActiveTransactions()
    {
        for (auto t : m_ActiveTransactions)
        {
            t->Update();
        }
    }

    void UnlinkFundsTransaction::CreateInitialTransactions()
    {
        //m_ActiveTransactions.push_back(static_cast<BaseTransaction::Creator&>(PushTransaction::Creator(m_withAssets)).Create(GetGateway(), m_WalletDB, GetTxID()));
    }
} // namespace beam::wallet::lelantus