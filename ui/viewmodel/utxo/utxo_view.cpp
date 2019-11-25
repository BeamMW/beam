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

#include "utxo_view.h"
#include "viewmodel/ui_helpers.h"
#include "model/app_model.h"
using namespace beam;
using namespace beam::wallet;
using namespace std;
using namespace beamui;

UtxoViewModel::UtxoViewModel()
    : _model{*AppModel::getInstance().getWallet()}
{
    connect(&_model, SIGNAL(allUtxoChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::Coin>&)),
        SLOT(onAllUtxoChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::Coin>&)));
    connect(&_model, SIGNAL(stateIDChanged()), SIGNAL(stateChanged()));

    _model.getAsync()->getUtxosStatus();
}


QAbstractItemModel* UtxoViewModel::getAllUtxos()
{
    return & _allUtxos;
}

QString UtxoViewModel::getCurrentHeight() const
{
    return QString::fromStdString(to_string(_model.getCurrentStateID().m_Height));
}

QString UtxoViewModel::getCurrentStateHash() const
{
    return QString(beam::to_hex(_model.getCurrentStateID().m_Hash.m_pData, 10).c_str());
}

void UtxoViewModel::onAllUtxoChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::Coin>& utxos)
{
    vector<shared_ptr<UtxoItem>> modifiedItems;
    modifiedItems.reserve(utxos.size());

    for (const auto& t : utxos)
    {
        modifiedItems.push_back(make_shared<UtxoItem>(t));
    }

    switch (action)
    {
    case ChangeAction::Reset:
    {
        _allUtxos.reset(modifiedItems);
        break;
    }

    case ChangeAction::Removed:
    {
        _allUtxos.remove(modifiedItems);
        break;
    }

    case ChangeAction::Added:
    {
        _allUtxos.insert(modifiedItems);
        break;
    }

    case ChangeAction::Updated:
    {
        _allUtxos.update(modifiedItems);
        break;
    }

    default:
        assert(false && "Unexpected action");
        break;
    }

    emit allUtxoChanged();
}
