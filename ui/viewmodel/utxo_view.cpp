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
#include "ui_helpers.h"
#include "model/app_model.h"
using namespace beam;
using namespace beam::wallet;
using namespace std;
using namespace beamui;

namespace
{
template<typename T>
bool compareUtxo(const T& lf, const T& rt, Qt::SortOrder sortOrder)
{
    if (sortOrder == Qt::DescendingOrder)
        return lf > rt;
    return lf < rt;
}
}

UtxoItem::UtxoItem(const beam::wallet::Coin& coin)
    : _coin{ coin }
{

}

UtxoItem::~UtxoItem()
{

}

QString UtxoItem::amount() const
{
    return AmountToString(_coin.m_ID.m_Value, Currencies::Beam);
}

QString UtxoItem::maturity() const
{
    if (!_coin.IsMaturityValid())
        return QString{ "-" };
    return QString::number(_coin.m_maturity);
}

UtxoViewStatus::EnStatus UtxoItem::status() const
{
    switch(_coin.m_status)
    {
        case Coin::Available:
            return UtxoViewStatus::Available;
        case Coin::Maturing:
            return UtxoViewStatus::Maturing;
        case Coin::Unavailable:
            return UtxoViewStatus::Unavailable;
        case Coin::Outgoing:
            return UtxoViewStatus::Outgoing;
        case Coin::Incoming:
			return UtxoViewStatus::Incoming;
        case Coin::Spent:
            return UtxoViewStatus::Spent;
        default:
            assert(false && "Unknown key type");
    }

    return UtxoViewStatus::Undefined;
}

UtxoViewType::EnType UtxoItem::type() const
{
    switch (_coin.m_ID.m_Type)
    {
        case Key::Type::Comission: return UtxoViewType::Comission;
        case Key::Type::Coinbase: return UtxoViewType::Coinbase;
        case Key::Type::Regular: return UtxoViewType::Regular;
        case Key::Type::Change: return UtxoViewType::Change;
        case Key::Type::Treasury: return UtxoViewType::Treasury;
    }

    return UtxoViewType::Undefined;
}

beam::Amount UtxoItem::rawAmount() const
{
    return _coin.m_ID.m_Value;
}

const beam::wallet::Coin::ID& UtxoItem::get_ID() const
{
	return _coin.m_ID;
}

beam::Height UtxoItem::rawMaturity() const
{
    return _coin.get_Maturity();
}


UtxoViewModel::UtxoViewModel()
    : _model{*AppModel::getInstance().getWallet()}
    , _sortOrder(Qt::DescendingOrder)
{
    connect(&_model, SIGNAL(allUtxoChanged(const std::vector<beam::wallet::Coin>&)),
        SLOT(onAllUtxoChanged(const std::vector<beam::wallet::Coin>&)));
    connect(&_model, SIGNAL(stateIDChanged()), SIGNAL(stateChanged()));

    _model.getAsync()->getUtxosStatus();
}

UtxoViewModel::~UtxoViewModel()
{
    qDeleteAll(_allUtxos);
}

QQmlListProperty<UtxoItem> UtxoViewModel::getAllUtxos()
{
    return QQmlListProperty<UtxoItem>(this, _allUtxos);
}

QString UtxoViewModel::getCurrentHeight() const
{
    return QString::fromStdString(to_string(_model.getCurrentStateID().m_Height));
}

QString UtxoViewModel::getCurrentStateHash() const
{
    return QString(beam::to_hex(_model.getCurrentStateID().m_Hash.m_pData, 10).c_str());
}

QString UtxoViewModel::sortRole() const
{
    return _sortRole;
}

void UtxoViewModel::setSortRole(const QString& value)
{
    _sortRole = value;
    sortUtxos();
}

Qt::SortOrder UtxoViewModel::sortOrder() const
{
    return _sortOrder;
}

void UtxoViewModel::setSortOrder(Qt::SortOrder value)
{
    _sortOrder = value;
    sortUtxos();
}

void UtxoViewModel::onAllUtxoChanged(const std::vector<beam::wallet::Coin>& utxos)
{
    // TODO: It's dirty hack. Should use QAbstractListModel instead of QQmlListProperty
    auto tmpList = _allUtxos;
    
    _allUtxos.clear();

    emit allUtxoChanged();

    qDeleteAll(tmpList);

    for (const auto& utxo : utxos)
    {
        _allUtxos.push_back(new UtxoItem(utxo));
    }

    sortUtxos();
}

void UtxoViewModel::sortUtxos()
{
    auto cmp = generateComparer();
    std::sort(_allUtxos.begin(), _allUtxos.end(), cmp);

    emit allUtxoChanged();
}

QString UtxoViewModel::amountRole() const
{
    return "amount";
}

QString UtxoViewModel::heightRole() const
{
    return "height";
}

QString UtxoViewModel::maturityRole() const
{
    return "maturity";
}

QString UtxoViewModel::statusRole() const
{
    return "status";
}

QString UtxoViewModel::typeRole() const
{
    return "type";
}

std::function<bool(const UtxoItem*, const UtxoItem*)> UtxoViewModel::generateComparer()
{
    if (_sortRole == amountRole())
        return [sortOrder = _sortOrder](const UtxoItem* lf, const UtxoItem* rt)
    {
        return compareUtxo(lf->rawAmount(), rt->rawAmount(), sortOrder);
    };

    if (_sortRole == maturityRole())
        return [sortOrder = _sortOrder](const UtxoItem* lf, const UtxoItem* rt)
    {
        return compareUtxo(lf->rawMaturity(), rt->rawMaturity(), sortOrder);
    };

    if (_sortRole == statusRole())
        return [sortOrder = _sortOrder](const UtxoItem* lf, const UtxoItem* rt)
    {
        return compareUtxo(lf->status(), rt->status(), sortOrder);
    };

    if (_sortRole == typeRole())
        return [sortOrder = _sortOrder](const UtxoItem* lf, const UtxoItem* rt)
    {
        return compareUtxo(lf->type(), rt->type(), sortOrder);
    };

    // defult
    return [sortOrder = _sortOrder](const UtxoItem* lf, const UtxoItem* rt)
    {
        return compareUtxo(lf->get_ID(), rt->get_ID(), sortOrder);
    };
}
