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

#include <QObject>
#include "wallet/wallet_client.h"
#include "utxo_view_status.h"
#include "utxo_view_type.h"

class UtxoItem : public QObject
{
    Q_OBJECT
        Q_PROPERTY(QString amount       READ getAmountWithCurrency     NOTIFY changed)
        Q_PROPERTY(QString maturity     READ maturity                  NOTIFY changed)
        Q_PROPERTY(int status           READ status                    NOTIFY changed)
        Q_PROPERTY(int type             READ type                      NOTIFY changed)
public:

    UtxoItem() = default;
    UtxoItem(const beam::wallet::Coin& coin);
    virtual ~UtxoItem();

    QString getAmountWithCurrency() const;
    QString getAmount() const;
    QString maturity() const;
    UtxoViewStatus::EnStatus status() const;
    UtxoViewType::EnType type() const;

    beam::Amount rawAmount() const;
    beam::Height rawMaturity() const;
    const beam::wallet::Coin::ID& get_ID() const;

signals:
    void changed();

private:
    beam::wallet::Coin _coin;
};

