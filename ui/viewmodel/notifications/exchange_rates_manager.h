// Copyright 2020 The Beam Team
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
#include "model/settings.h"
#include "wallet/client/extensions/news_channels/interface.h"
#include "viewmodel/currencies.h"   // WalletCurrency::Currency enum used in UI

class ExchangeRatesManager : public QObject
{
    Q_OBJECT
public:
    ExchangeRatesManager();

    QString getRateUnit() const;    
    QString calcAmountIn2ndCurrency(const QString&, beam::wallet::ExchangeRate::Currency) const;
    beam::wallet::ExchangeRate::Currency getRateUnitRaw() const;

    static beam::wallet::ExchangeRate::Currency convertCurrencyToExchangeCurrency(WalletCurrency::Currency uiCurrency);

public slots:
    void onExchangeRatesUpdate(const std::vector<beam::wallet::ExchangeRate>& rates);
    void onRateUnitChanged();

signals:
    void rateUnitChanged();
    void activeRateChanged();

private:
    QString getBeamRate() const;
    QString getBtcRate() const;
    QString getLtcRate() const;
    QString getQtumRate() const;
    void setRateUnit();

    WalletModel& m_walletModel;
    WalletSettings& m_settings;

    beam::wallet::ExchangeRate::Currency m_rateUnit;
    std::map<beam::wallet::ExchangeRate::Currency, beam::Amount> m_rates;
};
