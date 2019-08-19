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

#include "bitcoin_client_model.h"

#include "model/app_model.h"

using namespace beam;

namespace
{
    const int kUpdateInterval = 5000;
}


BitcoinClientModel::BitcoinClientModel(wallet::IWalletDB::Ptr walletDB, io::Reactor& reactor)
    : bitcoin::Client(walletDB, reactor)
    , m_timer(this)
    , m_walletModel(AppModel::getInstance().getWallet())
{
    qRegisterMetaType<beam::bitcoin::Client::Status>("beam::bitcoin::Client::Status");
    qRegisterMetaType<beam::bitcoin::Client::Balance>("beam::bitcoin::Client::Balance");

    if (!m_walletModel.expired())
    {
        connect(m_walletModel.lock().get(), SIGNAL(txStatus(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&)),
            SLOT(onTxStatus(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&)));
    }

    connect(&m_timer, SIGNAL(timeout()), this, SLOT(onTimer()));

    m_timer.start(kUpdateInterval);
}

double BitcoinClientModel::getAvailable()
{
    return m_balance.m_available;
}

double BitcoinClientModel::getReceiving()
{
    return m_receiving;
}

double BitcoinClientModel::getSending()
{
    return m_sending;
}

void BitcoinClientModel::OnStatus(Status status)
{
    emit gotStatus(status);
}

void BitcoinClientModel::OnBalance(const bitcoin::Client::Balance& balance)
{
    m_balance = balance;
    emit stateChanged();
}

void BitcoinClientModel::onTimer()
{
    // update balance
    GetAsync()->GetBalance();

    // connect to walletModel if we haven't connected yet
    if (m_walletModel.expired() && AppModel::getInstance().getWallet())
    {
        m_walletModel = AppModel::getInstance().getWallet();
        auto walletModelPtr = m_walletModel.lock();
        walletModelPtr->getAsync()->getWalletStatus();

        connect(walletModelPtr.get(), SIGNAL(txStatus(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&)),
            SLOT(onTxStatus(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&)));
    }
}

void BitcoinClientModel::onTxStatus(beam::wallet::ChangeAction action, const std::vector<beam::wallet::TxDescription>& txList)
{
    if (action == wallet::ChangeAction::Reset)
    {
        m_transactions.clear();
    }

    for (const auto& transaction : txList)
    {
        const auto txId = transaction.GetTxID();
        if (txId && 
            transaction.m_txType == wallet::TxType::AtomicSwap)
        {
            switch (action)
            {
            case wallet::ChangeAction::Reset:
            case wallet::ChangeAction::Added:
            {
                m_transactions.emplace(*txId, transaction);
                break;
            }
            case wallet::ChangeAction::Updated:
            {
                const auto it = m_transactions.find(*txId);
                if (it != m_transactions.end())
                {
                    m_transactions[*txId] = transaction;
                }
                else
                {
                    m_transactions.emplace(*txId, transaction);
                }
                break;
            }
            // TODO: check, may be unused
            case wallet::ChangeAction::Removed:
            {
                auto it = m_transactions.find(*txId);
                if (it != m_transactions.end())
                {
                    m_transactions.erase(it);
                }
                break;
            }
            default:
            {
                assert(false && "unexpected ChangeAction");
                break;
            }
            }
        }
    }

    // recalculate sending / receiving
    RecalculateAmounts();
}

void BitcoinClientModel::RecalculateAmounts()
{
    m_sending = 0;
    m_receiving = 0;

    for (const auto&[txId, txDescription] : m_transactions)
    {
        if (txDescription.m_status == wallet::TxStatus::InProgress)
        {
            auto isBeamSide = txDescription.GetParameter<bool>(wallet::TxParameterID::AtomicSwapIsBeamSide);
            auto swapAmount = txDescription.GetParameter<Amount>(wallet::TxParameterID::AtomicSwapAmount);
            assert(isBeamSide && swapAmount);
            // if (txDescription.)
            if (*isBeamSide)
            {
                m_receiving += double(*swapAmount) / 100000000;
            }
            else
            {
                m_sending += double(*swapAmount) / 100000000;
            }
        }
    }

    emit stateChanged();
}
