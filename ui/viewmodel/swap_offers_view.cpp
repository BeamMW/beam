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

#include "model/app_model.h"
#include "ui_helpers.h"
#include "swap_offers_view.h"
using namespace beam;
using namespace beam::wallet;
using namespace std;
using namespace beamui;

QDateTime SwapOfferItem::time() const
{
    QDateTime datetime;
    datetime.setTime_t(m_offer.m_modifyTime);
    return datetime;
}

QString SwapOfferItem::id() const
{
    auto id = to_hex(m_offer.m_txId.data(), m_offer.m_txId.size());
    return QString::fromStdString(id);
}

QString SwapOfferItem::amount() const
{
    return BeamToString(m_offer.m_amount);
}

QString SwapOfferItem::status() const
{
    return m_offer.getStatusString().c_str();
}

beam::Amount SwapOfferItem::rawAmount() const
{
    return m_offer.m_amount;
}

QString SwapOfferItem::message() const
{
    std::string msg;
    if(fromByteBuffer(m_offer.m_message, msg))
        return QString::fromStdString(msg);
    else
        return QString();
}

QHash<int, QByteArray> SwapOffersList::roleNames() const
{
    static const auto roles = QHash<int, QByteArray>
    {
        { static_cast<int>(Roles::TimeRole), "time" },
        { static_cast<int>(Roles::TimeSortRole), "timeSort" },
        { static_cast<int>(Roles::IdRole), "id" },
        { static_cast<int>(Roles::IdSortRole), "idSort" },
        { static_cast<int>(Roles::AmountRole), "amount" },
        { static_cast<int>(Roles::AmountSortRole), "amountSort" },
        { static_cast<int>(Roles::StatusRole), "status" },
        { static_cast<int>(Roles::StatusSortRole), "statusSort" },
        { static_cast<int>(Roles::MessageRole), "message" },
        { static_cast<int>(Roles::MessageSortRole), "messageSort" }
    };
    return roles;
}

QVariant SwapOffersList::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_list.size())
    {
       return QVariant();
    }
    auto& value = m_list[index.row()];
    switch (static_cast<Roles>(role))
    {
    case Roles::TimeRole:
    case Roles::TimeSortRole:
        return value->time();
    case Roles::IdRole:
    case Roles::IdSortRole:
        return value->id();
	case Roles::AmountRole:
        return value->amount();
    case Roles::AmountSortRole:
        return value->rawAmount();
    case Roles::StatusRole:
    case Roles::StatusSortRole:
        return value->status();
    case Roles::MessageRole:
    case Roles::MessageSortRole:
        return value->message();
    default:
        return QVariant();
    }
}

SwapOffersViewModel::SwapOffersViewModel()
    : m_walletModel{*AppModel::getInstance().getWallet()}
{
    connect(&m_walletModel,
            SIGNAL(swapOffersChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::SwapOffer>&)),
            SLOT(onSwapDataModelChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::SwapOffer>&)));

    m_walletModel.getAsync()->getSwapOffers();
}

SwapOffersViewModel::~SwapOffersViewModel()
{
    // no raw pointers
}

QAbstractItemModel* SwapOffersViewModel::getAllOffers()
{
    return &m_offersList;
}

void SwapOffersViewModel::sendSwapOffer(double amount, QString msg)
{
    static TxID id = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    SwapOffer newOffer;
    newOffer.m_createTime = newOffer.m_modifyTime = getTimestamp();
    newOffer.m_amount = std::round(amount * beam::Rules::Coin);
    newOffer.m_txId = id;
    id[15]++;
    newOffer.m_message = toByteBuffer(msg.toStdString());

    m_walletModel.getAsync()->sendSwapOffer(move(newOffer));
}

void SwapOffersViewModel::onSwapDataModelChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::SwapOffer>& offers)
{
    vector<shared_ptr<SwapOfferItem>> newOffers;
    newOffers.reserve(offers.size());

    for (const auto& offer : offers)
    {
        newOffers.push_back(make_shared<SwapOfferItem>(offer));
    }

    switch (action)
    {
    case ChangeAction::Reset:
        {
            m_offersList.reset(newOffers);
            break;
        }

    case ChangeAction::Added:
        {
            m_offersList.insert(newOffers);
            break;
        }
    
    default:
        assert(false && "Unexpected action");
        break;
    }
    
    emit allOffersChanged();
}
