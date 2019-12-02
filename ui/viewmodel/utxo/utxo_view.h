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
#include "model/wallet_model.h"
#include "utxo_item_list.h"

class UtxoViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel* allUtxos  READ getAllUtxos          NOTIFY allUtxoChanged)
    Q_PROPERTY(QString currentHeight         READ getCurrentHeight     NOTIFY stateChanged)
    Q_PROPERTY(QString currentStateHash      READ getCurrentStateHash  NOTIFY stateChanged)

public:
    UtxoViewModel();
    QAbstractItemModel* getAllUtxos();
    QString getCurrentHeight() const;
    QString getCurrentStateHash() const;
public slots:
    void onAllUtxoChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::Coin>& utxos);
signals:
    void allUtxoChanged();
    void stateChanged();
private:
    UtxoItemList _allUtxos;
    WalletModel& _model;
};