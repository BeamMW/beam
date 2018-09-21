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

UtxoItem::UtxoItem(const beam::Coin& coin)
    : _coin{ coin }
{

}

QString UtxoItem::amount() const
{
    return BeamToString(_coin.m_amount) + " BEAM";
}

QString UtxoItem::height() const
{
    return QString::number(_coin.m_createHeight);
}

QString UtxoItem::maturity() const
{
    if (_coin.m_maturity == static_cast<Height>(-1))
        return QString{ "-" };
    return QString::number(_coin.m_maturity);
}

QString UtxoItem::status() const
{
    static const char* Names[] =
    {
        "Unconfirmed",
        "Unspent",
        "Locked",
        "Spent",
        "Draft"
    };
    return Names[_coin.m_status];
}

QString UtxoItem::type() const
{
    static const char* Names[] =
    {
        "Comission",
        "Coinbase",
        "Kernel",
        "Regular",
        "Identity",
        "SChannelNonce"
    };
    return Names[static_cast<int>(_coin.m_key_type)];
}

beam::Amount UtxoItem::rawAmount() const
{
    return _coin.m_amount;
}

beam::Height UtxoItem::rawHeight() const
{
    return _coin.m_createHeight;
}

beam::Height UtxoItem::rawMaturity() const
{
    return _coin.m_maturity;
}


UtxoViewModel::UtxoViewModel()
    : _model{*AppModel::getInstance()->getWallet()}
    , _sortOrder(Qt::DescendingOrder)
{
    connect(&_model, SIGNAL(onAllUtxoChanged(const std::vector<beam::Coin>&)),
        SLOT(onAllUtxoChanged(const std::vector<beam::Coin>&)));
    connect(&_model, SIGNAL(onStatus(const WalletStatus&)), SLOT(onStatus(const WalletStatus&)));

    if (_model.async)
    {
        _model.async->getUtxosStatus();
    }
}

UtxoViewModel::~UtxoViewModel()
{

}

QQmlListProperty<UtxoItem> UtxoViewModel::getAllUtxos()
{
    return QQmlListProperty<UtxoItem>(this, _allUtxos);
}

QString UtxoViewModel::getCurrentHeight() const
{
    return _currentHeight;
}

QString UtxoViewModel::getCurrentStateHash() const
{
    return _currentStateHash;
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

void UtxoViewModel::onAllUtxoChanged(const std::vector<beam::Coin>& utxos)
{
    _allUtxos.clear();

    for (const auto& utxo : utxos)
    {
        _allUtxos.push_back(new UtxoItem(utxo));
    }

    sortUtxos();
}

void UtxoViewModel::onStatus(const WalletStatus& status)
{
    _currentHeight = QString::asprintf("%ld", status.stateID.m_Height);
    _currentStateHash = QString(beam::to_hex(status.stateID.m_Hash.m_pData, 10).c_str());
    emit stateChanged();
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

    // defult for heightRole
    return [sortOrder = _sortOrder](const UtxoItem* lf, const UtxoItem* rt)
    {
        return compareUtxo(lf->rawHeight(), rt->rawHeight(), sortOrder);
    };
}
