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

    qRegisterMetaType<std::vector<beam::wallet::ExchangeRate>>("std::vector<beam::wallet::ExchangeRate>");

    connect(&m_walletModel,
            SIGNAL(exchangeRatesUpdate(const std::vector<beam::wallet::ExchangeRate>&)),
            SLOT(onExchangeRatesUpdate(const std::vector<beam::wallet::ExchangeRate>&)));

    connect(&m_settings,
            SIGNAL(rateUnitChanged()),
            SLOT(onRateUnitChanged()));

    setRateUnit();
}

void ExchangeRatesManager::setRateUnit()
{
    m_rateUnit = ExchangeRate::from_string(m_settings.getRateUnit().toStdString());
    if (m_rateUnit == ExchangeRate::Currency::Unknown)
    {
        m_rateUnit = ExchangeRate::Currency::Usd;   // set USD as default
    }
    m_walletModel.getAsync()->getExchangeRates();
}

void ExchangeRatesManager::onExchangeRatesUpdate(const std::vector<beam::wallet::ExchangeRate>& rates)
{
    bool isActiveRateChanged = false;

    for (const auto& rate : rates)
    {
        LOG_DEBUG() << "Exchange rate: 1 " << beam::wallet::ExchangeRate::to_string(rate.m_currency) << " = "
                    << rate.m_rate << " " << beam::wallet::ExchangeRate::to_string(rate.m_unit);

        if (rate.m_unit != m_rateUnit) continue;
        m_rates[rate.m_currency] = rate.m_rate;
        if (!isActiveRateChanged) isActiveRateChanged = true;
    }
    if (isActiveRateChanged) emit activeRateChanged();
}

void ExchangeRatesManager::onRateUnitChanged()
{
    setRateUnit();
    emit rateUnitChanged();
}

beam::wallet::ExchangeRate::Currency ExchangeRatesManager::getRateUnitRaw() const
{
    return m_rateUnit;
}

beam::Amount ExchangeRatesManager::getBeamRate() const
{
    const auto it = m_rates.find(ExchangeRate::Currency::Beam);
    return (it == std::cend(m_rates)) ? 0 : it->second;
}

beam::Amount ExchangeRatesManager::getBtcRate() const
{
    const auto it = m_rates.find(ExchangeRate::Currency::Bitcoin);
    return (it == std::cend(m_rates)) ? 0 : it->second;
}

beam::Amount ExchangeRatesManager::getLtcRate() const
{
    const auto it = m_rates.find(ExchangeRate::Currency::Litecoin);
    return (it == std::cend(m_rates)) ? 0 : it->second;
}

beam::Amount ExchangeRatesManager::getQtumRate() const
{
    const auto it = m_rates.find(ExchangeRate::Currency::Qtum);
    return (it == std::cend(m_rates)) ? 0 : it->second;
}

/**
 *  Calculate amount in second currency
 *  @amount     Amount in main currency
 *  @currency   Main currency
 *  @return     Equal value in second currency
 */
QString ExchangeRatesManager::calcAmountIn2ndCurrency(const QString& amount, ExchangeRate::Currency currency) const
{
    beam::Amount rate;
    // TODO: return "-" if no info about exchange rate
    // TODO: return with currency suffix
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
            rate = 0;
    }

    if (rate == 0)
    {
        return "-";
    }
    else
    {
        return QMLGlobals::multiplyWithPrecision8(amount, beamui::AmountToUIString(rate));
    }
}

ExchangeRate::Currency ExchangeRatesManager::convertCurrencyToExchangeCurrency(WalletCurrency::Currency uiCurrency)
{
    switch (uiCurrency)
    {
    case Currency::CurrBeam:
        return ExchangeRate::Currency::Beam;
    case Currency::CurrBtc:
        return ExchangeRate::Currency::Bitcoin;
    case Currency::CurrLtc:
        return ExchangeRate::Currency::Litecoin;
    case Currency::CurrQtum:
        return ExchangeRate::Currency::Qtum;
    default:
        return ExchangeRate::Currency::Unknown;
    }
}

