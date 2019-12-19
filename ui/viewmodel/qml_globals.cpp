// Copyright 2019 The Beam Team
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
#include "qml_globals.h"
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include "version.h"
#include "model/app_model.h"
#include "wallet/common.h"
#include "ui_helpers.h"
#include "wallet/bitcoin/bitcoin_side.h"
#include "wallet/litecoin/litecoin_side.h"
#include "wallet/qtum/qtum_side.h"
#include "utility/string_helpers.h"

#include <boost/algorithm/string.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include "3rdparty/libbitcoin/include/bitcoin/bitcoin/formats/base_10.hpp"

using boost::multiprecision::cpp_dec_float_50;

namespace
{
    const int kDefaultFeeInGroth = 10;
    const int kFeeInGroth_Fork1 = 100;

    template <char C>
    bool char_is(const char c)
    {
        return c == C;
    }

    template<uint8_t N>
    QString rountWithPrecision(const QString& number)
    {
        //TODO rounding percision
        const char delimeter = '.';
        auto parts = string_helpers::split(number.toStdString(), delimeter);

        std::string result;
        std::ostringstream oss;
        if (parts.size() == 2)
        {    
            cpp_dec_float_50 afterPoint("0." + parts[1]);

            std::ostringstream afterPointOss;
            afterPointOss.precision(N);
            afterPointOss << std::fixed << afterPoint;

            auto afterPointParts = string_helpers::split(afterPointOss.str(), delimeter);
            oss << parts[0] << delimeter << (afterPointParts.size() > 1 ? afterPointParts[1] : "0");
            result = oss.str();
            boost::algorithm::trim_right_if(result, char_is<'0'>);
            boost::algorithm::trim_right_if(result, char_is<'.'>);
        }
        else
        {
            oss << parts[0];
            result = oss.str();
        }

        return QString::fromStdString(result);
    }
}

QMLGlobals::QMLGlobals(QQmlEngine& engine)
    : _engine(engine)
{
}

void QMLGlobals::showMessage(const QString& message)
{
    QMessageBox::information(nullptr, "BeamWalletUI", message);
}

void QMLGlobals::copyToClipboard(const QString& text)
{
    QApplication::clipboard()->setText(text);
}


QString QMLGlobals::version()
{
    return QString::fromStdString(PROJECT_VERSION);
}

bool QMLGlobals::isTAValid(const QString& text)
{
    return QMLGlobals::isTransactionToken(text) || QMLGlobals::isAddress(text);
}

bool QMLGlobals::isAddress(const QString& text)
{
    return beam::wallet::check_receiver_address(text.toStdString());
}

bool QMLGlobals::isTransactionToken(const QString& text)
{
    if (text.isEmpty()) return false;
    
    auto params = beam::wallet::ParseParameters(text.toStdString());
    return params && params->GetParameter<beam::wallet::TxType>(beam::wallet::TxParameterID::TransactionType);
}

bool QMLGlobals::isSwapToken(const QString& text)
{
    if (text.isEmpty()) return false;
    
    auto params = beam::wallet::ParseParameters(text.toStdString());
    if (!params)
    {
        return false;
    }
    auto type = params->GetParameter<beam::wallet::TxType>(beam::wallet::TxParameterID::TransactionType);
    return type && *type == beam::wallet::TxType::AtomicSwap;
}

QString QMLGlobals::getLocaleName()
{
    const auto& settings = AppModel::getInstance().getSettings();
    return settings.getLocale();
}

int QMLGlobals::maxCommentLength()
{
    return 1024;
}

bool QMLGlobals::isFeeOK(uint32_t fee, Currency currency)
{
    switch (currency)
    {
    case Currency::CurrBeam: return fee >= minFeeBeam();
    case Currency::CurrBtc:  return true;
    case Currency::CurrLtc:  return true;
    case Currency::CurrQtum: return true;
    default:
        assert(false);
        return false;
    }
}

uint32_t QMLGlobals::minFeeBeam()
{
    assert(AppModel::getInstance().getWallet());
    return AppModel::getInstance().getWallet()->isFork1() ? kFeeInGroth_Fork1 : kDefaultFeeInGroth;
}

uint32_t QMLGlobals::defFeeBeam()
{
    return minFeeBeam();
}

uint32_t QMLGlobals::defFeeRateBtc()
{
    const auto btcSettings = AppModel::getInstance().getBitcoinClient()->GetSettings();
    return btcSettings.GetFeeRate();
}

uint32_t QMLGlobals::defFeeRateLtc()
{
    const auto ltcSettings = AppModel::getInstance().getLitecoinClient()->GetSettings();
    return ltcSettings.GetFeeRate();
}

uint32_t QMLGlobals::defFeeRateQtum()
{
    return AppModel::getInstance().getQtumClient()->GetSettings().GetFeeRate();
}

bool QMLGlobals::needPasswordToSpend()
{
    return AppModel::getInstance().getSettings().isPasswordReqiredToSpendMoney();
}

bool QMLGlobals::isPasswordValid(const QString& value)
{
    beam::SecString secretPass = value.toStdString();
    return AppModel::getInstance().checkWalletPassword(secretPass);
}

QString QMLGlobals::beamFeeRateLabel()
{
    return "GROTH";
}

QString QMLGlobals::btcFeeRateLabel()
{
    return "sat/kB";
}

QString QMLGlobals::ltcFeeRateLabel()
{
    return "ph/kB";
}

QString QMLGlobals::qtumFeeRateLabel()
{
    return "qsat/kB";
}

int QMLGlobals::getMinFeeOrRate(Currency currency)
{
    switch (currency) {
        case Currency::CurrBeam: return minFeeBeam();
        default: return 0;
    }
}

QString QMLGlobals::calcTotalFee(Currency currency, unsigned int feeRate)
{
    switch (currency) {
        case Currency::CurrBeam: {
            return QString::fromStdString(std::to_string(feeRate));
        }
        case Currency::CurrBtc: {
            auto total = beam::wallet::BitcoinSide::CalcTotalFee(feeRate);
            return QString::fromStdString(std::to_string(total)) + " sat";
        }
        case Currency::CurrLtc: {
            auto total = beam::wallet::LitecoinSide::CalcTotalFee(feeRate);
            return QString::fromStdString(std::to_string(total)) + " ph";
        }
        case Currency::CurrQtum: {
            auto total = beam::wallet::QtumSide::CalcTotalFee(feeRate);
            return QString::fromStdString(std::to_string(total)) + " qsat";
        }
        default: {
            assert(false);
            return QString();
        }
    }
}

bool QMLGlobals::canSwap()
{
    return haveBtc() || haveLtc() || haveQtum();
}

bool QMLGlobals::haveBtc()
{
    return AppModel::getInstance().getBitcoinClient()->GetSettings().IsActivated();
}

bool QMLGlobals::haveLtc()
{
    return AppModel::getInstance().getLitecoinClient()->GetSettings().IsActivated();
}

bool QMLGlobals::haveQtum()
{
    return AppModel::getInstance().getQtumClient()->GetSettings().IsActivated();
}

QString QMLGlobals::rawTxParametrsToTokenStr(QVariant variantTxParams)
{
    if (!variantTxParams.isNull() && variantTxParams.isValid())
    {
        auto txParameters = variantTxParams.value<beam::wallet::TxParameters>();
        return QString::fromStdString(std::to_string(txParameters));
    }
    return "";
}

bool QMLGlobals::canReceive(Currency currency)
{
    switch(currency)
    {
    case Currency::CurrBeam:
    {
        return true;
    }
    case Currency::CurrBtc:
    {
        auto client = AppModel::getInstance().getBitcoinClient();
        return client->GetSettings().IsActivated() &&
               client->getStatus() == beam::bitcoin::Client::Status::Connected;
    }
    case Currency::CurrLtc:
    {
        auto client = AppModel::getInstance().getLitecoinClient();
        return client->GetSettings().IsActivated() &&
               client->getStatus() == beam::bitcoin::Client::Status::Connected;
    }
    case Currency::CurrQtum:
    {
        auto client = AppModel::getInstance().getQtumClient();
        return client->GetSettings().IsActivated() &&
               client->getStatus() == beam::bitcoin::Client::Status::Connected;
    }
    default:
    {
        assert(false);
        return false;
    }
    }
}

QString QMLGlobals::getCurrencyName(Currency currency)
{
    switch(currency)
    {
    case Currency::CurrBeam:
    {
        //% "BEAM"
        return qtTrId("general-beam");
    }
    case Currency::CurrBtc:
    {
        //% "Bitcoin"
        return qtTrId("general-bitcoin");
    }
    case Currency::CurrLtc:
    {
        //% "Litecoin"
        return qtTrId("general-litecoin");
    }
    case Currency::CurrQtum:
    {
        //% "QTUM"
        return qtTrId("general-qtum");
    }
    default:
    {
        assert(false && "unexpected swap coin!");
        return QString();
    }
    }
}

bool QMLGlobals::isSwapFeeOK(unsigned int amount, unsigned int fee, Currency currency)
{
    switch (currency) {
        case Currency::CurrBeam: {
            return amount > fee && fee >= QMLGlobals::minFeeBeam();
        }
        case Currency::CurrBtc: {
            return beam::wallet::BitcoinSide::CheckAmount(amount, fee);
        }
        case Currency::CurrLtc: {
            return beam::wallet::LitecoinSide::CheckAmount(amount, fee);
        }
        case Currency::CurrQtum: {
            return beam::wallet::QtumSide::CheckAmount(amount, fee);
        }
        default: {
            assert(false);
            return true;
        }
    }
}

QString QMLGlobals::divideWithPrecision8(const QString& dividend, const QString& divider)
{
    cpp_dec_float_50 dec_dividend(dividend.toStdString().c_str());
    cpp_dec_float_50 dec_divider(divider.toStdString().c_str());

    cpp_dec_float_50 quotient = dec_dividend / dec_divider;

    std::ostringstream oss;
    oss.precision(std::numeric_limits<cpp_dec_float_50>::digits10);
    oss << std::fixed << quotient;

    QString result = QString::fromStdString(oss.str());
    return QMLGlobals::rountWithPrecision8(result);
}

QString QMLGlobals::multiplyWithPrecision8(const QString& first, const QString& second)
{
    cpp_dec_float_50 dec_first(first.toStdString().c_str());
    cpp_dec_float_50 dec_second(second.toStdString().c_str());

    cpp_dec_float_50 product = dec_first * dec_second;

    std::ostringstream oss;
    oss.precision(std::numeric_limits<cpp_dec_float_50>::digits10);
    oss << std::fixed << product;

    QString result = QString::fromStdString(oss.str());
    return QMLGlobals::rountWithPrecision8(result);
}

QString QMLGlobals::rountWithPrecision8(const QString& number)
{
    return rountWithPrecision<libbitcoin::btc_decimal_places>(number);
}
