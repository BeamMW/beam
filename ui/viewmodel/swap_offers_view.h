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
#include <QQmlListProperty>
#include "model/wallet_model.h"
#include "helpers/list_model.h"

class SwapOfferItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString amount READ amount NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    SwapOfferItem() = default;
    SwapOfferItem(const beam::wallet::TxDescription& offer) : m_offer{offer} {};

    QString amount() const;
    QString status() const;

    beam::Amount rawAmount() const;

signals:
    void changed();

private:
    beam::wallet::TxDescription m_offer;
};

class SwapOffersList : public ListModel<std::shared_ptr<SwapOfferItem>>
{

	Q_OBJECT
public:

	enum class Roles
	{
		AmountRole = Qt::UserRole + 1,
		AmountSortRole,

		StatusRole,
		StatusSortRole
	};
	SwapOffersList() {};

	QVariant data(const QModelIndex& index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;
};

class SwapOffersViewModel : public QObject
{
	Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* allOffers READ getAllOffers NOTIFY allOffersChanged)

public:
    SwapOffersViewModel();
    virtual ~SwapOffersViewModel();

    QAbstractItemModel* getAllOffers();
    // void sendTestOffer();

    Q_INVOKABLE void sendTestOffer();

public slots:
    void onAllOffersChanged(const std::vector<beam::wallet::TxDescription>& offers);

signals:
    void allOffersChanged();

private:
    WalletModel& m_walletModel;
    SwapOffersList m_offersList;
};
