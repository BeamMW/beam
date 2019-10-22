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

#include "model/wallet_model.h"
#include <map>
#include <QObject>

class TokenBootstrapManager : public QObject
{
    Q_OBJECT

public:
    TokenBootstrapManager();
    ~TokenBootstrapManager();

    Q_INVOKABLE void checkTokenForDuplicate(const QString& token);

public slots:
    void onTxStatus(beam::wallet::ChangeAction action,
                    const std::vector<beam::wallet::TxDescription>& items);

signals:   
    void tokenPreviousAccepted(const QString& token);
    void tokenFirstTimeAccepted(const QString& token);
    void tokenOwnGenerated(const QString& token);

private:
    void checkIsTxPreviousAccepted();

    WalletModel& _wallet_model;
    std::map<beam::wallet::TxID, QString> _tokensInProgress;
    std::vector<beam::wallet::TxID> _myTxIds;
};
