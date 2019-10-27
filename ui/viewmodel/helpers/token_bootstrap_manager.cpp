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

#include "token_bootstrap_manager.h"

#include "model/app_model.h"

#include <iterator>

TokenBootstrapManager::TokenBootstrapManager()
    : _wallet_model(*AppModel::getInstance().getWallet())
{
    connect(
        &_wallet_model,
        SIGNAL(transactionsChanged(beam::wallet::ChangeAction,
                        const std::vector<beam::wallet::TxDescription>&)),
        SLOT(onTransactionsChanged(beam::wallet::ChangeAction,
                        const std::vector<beam::wallet::TxDescription>&)));
    _wallet_model.getAsync()->getTransactions();
}

TokenBootstrapManager::~TokenBootstrapManager() {}

void TokenBootstrapManager::onTransactionsChanged(
    beam::wallet::ChangeAction action,
    const std::vector<beam::wallet::TxDescription>& items)
{
    switch (action)
    {
    case beam::wallet::ChangeAction::Reset:
        _myTxIds.clear(); // no break
    case beam::wallet::ChangeAction::Added:
    case beam::wallet::ChangeAction::Updated:
        for (const auto& item : items)
        {
            if (const auto& id = item.GetTxID(); id)
            {
                _myTxIds.insert(*id);
            }
        }
        break;
    case beam::wallet::ChangeAction::Removed:
        for (const auto& item : items)
        {
            if (const auto& id = item.GetTxID(); id)
            {
                _myTxIds.erase(*id);
            }
        }
        break;
    }

    checkIsTxPreviousAccepted();
}

void TokenBootstrapManager::checkTokenForDuplicate(const QString& token)
{
    auto parameters = beam::wallet::ParseParameters(token.toStdString());
    if (!parameters)
    {
        LOG_ERROR() << "Can't parse token params";
        return;
    }

    auto parametrsValue = parameters.value();
    auto peerID = parametrsValue.GetParameter<beam::wallet::WalletID>(
        beam::wallet::TxParameterID::PeerID);
    if (peerID && _wallet_model.isOwnAddress(*peerID))
    {
        emit tokenOwnGenerated(token);
        return;
    }

    auto txId = parametrsValue.GetTxID();
    if (!txId)
    {
        LOG_ERROR() << "Empty tx id in txParams";
        return;
    }
    auto txIdValue = txId.value();
    _tokensInProgress[txIdValue] = token;

    _myTxIds.empty()
        ? _wallet_model.getAsync()->getTransactions()
        : checkIsTxPreviousAccepted();
}

void TokenBootstrapManager::checkIsTxPreviousAccepted()
{
    if (!_tokensInProgress.empty())
    {
        for (const auto& txId : _myTxIds)
        {
            const auto& it = _tokensInProgress.find(txId);
            if (it != _tokensInProgress.end())
            {
                emit tokenPreviousAccepted(it->second);
                _tokensInProgress.erase(it);
            }
        }

        for (const auto& token : _tokensInProgress)
        {
            emit tokenFirstTimeAccepted(token.second);
        }
        _tokensInProgress.clear();
    }
}
