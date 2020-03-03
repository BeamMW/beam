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

#include "exchange_rates_manager.h"

#include "model/app_model.h"
#include "viewmodel/ui_helpers.h"
#include "viewmodel/qml_globals.h"

// test
#include "utility/logger.h"

using namespace beam::wallet;

ExchangeRatesManager::ExchangeRatesManager()
    : m_walletModel(*AppModel::getInstance().getWallet())
    , m_settings(AppModel::getInstance().getSettings())
{
    setAmountUnit();

    qRegisterMetaType<std::vector<beam::wallet::ExchangeRate>>("std::vector<beam::wallet::ExchangeRate>");

    connect(&m_walletModel,
            SIGNAL(exchangeRatesUpdate(const std::vector<beam::wallet::ExchangeRate>&)),
            SLOT(onExchangeRatesUpdate(const std::vector<beam::wallet::ExchangeRate>&)));

    connect(&m_settings,
            SIGNAL(amountUnitChanged()),
            SLOT(onAmountUnitChanged()));

    m_walletModel.getAsync()->getExchangeRates();
}

void ExchangeRatesManager::setAmountUnit()
{
    m_rateUnit = ExchangeRate::from_string(m_settings.getAmountUnit().toStdString());
    if (m_rateUnit == ExchangeRate::Currency::Unknown)
    {
        m_rateUnit = ExchangeRate::Currency::Usd;
    }
}

void ExchangeRatesManager::onExchangeRatesUpdate(const std::vector<beam::wallet::ExchangeRate>& rates)
{
    for (const auto& rate : rates)
    {
        LOG_DEBUG() << "Exchange rate: 1 " << beam::wallet::ExchangeRate::to_string(rate.m_currency) << " = "
                    << rate.m_rate << " " << beam::wallet::ExchangeRate::to_string(rate.m_unit);

        if (rate.m_unit != m_rateUnit) continue;
        m_rates[rate.m_currency] = rate.m_rate;
    }
}

void ExchangeRatesManager::onAmountUnitChanged()
{
    setAmountUnit();
}

QString ExchangeRatesManager::getBeamRate() const
{
    const auto it = m_rates.find(ExchangeRate::Currency::Beam);
    beam::Amount value = (it == std::cend(m_rates)) ? 0 : it->second;
    
    return beamui::AmountToUIString(value);
}

QString ExchangeRatesManager::getBtcRate() const
{
    const auto it = m_rates.find(ExchangeRate::Currency::Bitcoin);
    beam::Amount value = (it == std::cend(m_rates)) ? 0 : it->second;
    
    return beamui::AmountToUIString(value);
}

QString ExchangeRatesManager::getLtcRate() const
{
    const auto it = m_rates.find(ExchangeRate::Currency::Litecoin);
    beam::Amount value = (it == std::cend(m_rates)) ? 0 : it->second;
    
    return beamui::AmountToUIString(value);
}

QString ExchangeRatesManager::getQtumRate() const
{
    const auto it = m_rates.find(ExchangeRate::Currency::Qtum);
    beam::Amount value = (it == std::cend(m_rates)) ? 0 : it->second;
    
    return beamui::AmountToUIString(value);
}

/**
 *  Calculate amount in second currency
 *  @amount     Amount in main currency
 *  @currency   Main currency
 *  @return     Equal value in second currency
 */
QString ExchangeRatesManager::calcAmount(const QString& amount, ExchangeRate::Currency currency) const
{
    QString rate;
    switch (currency)
    {
        case ExchangeRate::Currency::Beam:
            rate = getBeamRate();
            break;
        case ExchangeRate::Currency::Bitcoin:
            rate = getBtcRate();
            break;
        case ExchangeRate::Currency::Litecoin:
            rate = getLtcRate();
            break;
        case ExchangeRate::Currency::Qtum:
            rate = getQtumRate();
            break;
        default:
            return "0";
    }
    return QMLGlobals::multiplyWithPrecision8(amount, rate);
}

