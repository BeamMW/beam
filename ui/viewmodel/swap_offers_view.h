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
#include <QDateTime>
#include <QQmlListProperty>
#include "model/wallet_model.h"
#include "helpers/list_model.h"

class SwapOfferItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QDateTime time   READ time       NOTIFY changed)
    Q_PROPERTY(QString id       READ id         NOTIFY changed)
    Q_PROPERTY(QString amount   READ amount     NOTIFY changed)
    Q_PROPERTY(QString status   READ status     NOTIFY changed)
    Q_PROPERTY(QString message  READ message    NOTIFY changed)
public:
    SwapOfferItem() = default;
    SwapOfferItem(const beam::wallet::SwapOffer& offer) : m_offer{offer} {};

    QDateTime time() const;
    QString id() const;
    QString amount() const;
    QString status() const;
    QString message() const;

    beam::Amount rawAmount() const;

signals:
    void changed();

private:
    beam::wallet::SwapOffer m_offer;
};

class SwapOffersList : public ListModel<std::shared_ptr<SwapOfferItem>>
{

	Q_OBJECT
public:

	enum class Roles
	{
        TimeRole = Qt::UserRole + 1,
        TimeSortRole,
        IdRole,
        IdSortRole,
		AmountRole,
		AmountSortRole,
		StatusRole,
		StatusSortRole,
		MessageRole,
		MessageSortRole
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

    Q_INVOKABLE void sendSwapOffer(double amount, QString msg);

public slots:
    void onSwapDataModelChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::SwapOffer>& offers);

signals:
    void allOffersChanged();

private:
    WalletModel& m_walletModel;
    SwapOffersList m_offersList;
};
