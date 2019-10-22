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

namespace
{
    const int kDefaultFeeInGroth = 10;
    const int kFeeInGroth_Fork1 = 100;
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
    if (QMLGlobals::isTransactionToken(text))
    {
        return true;
    }

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
    case Currency::CurrBtc:  return fee >= minFeeRateBtc();
    case Currency::CurrLtc:  return fee >= minFeeRateLtc();
    case Currency::CurrQtum: return fee >= minFeeRateQtum();
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

uint32_t QMLGlobals::minFeeRateBtc()
{
     const auto btcSettings = AppModel::getInstance().getBitcoinClient()->GetSettings();
     return btcSettings.GetMinFeeRate();
}

uint32_t QMLGlobals::defFeeRateBtc()
{
     const auto btcSettings = AppModel::getInstance().getBitcoinClient()->GetSettings();
     return btcSettings.GetFeeRate();
}

uint32_t QMLGlobals::minFeeRateLtc()
{
    const auto ltcSettings = AppModel::getInstance().getLitecoinClient()->GetSettings();
    return ltcSettings.GetMinFeeRate();
}

uint32_t QMLGlobals::defFeeRateLtc()
{
    const auto ltcSettings = AppModel::getInstance().getLitecoinClient()->GetSettings();
    return ltcSettings.GetFeeRate();
}

uint32_t QMLGlobals::minFeeRateQtum()
{
    return AppModel::getInstance().getQtumClient()->GetSettings().GetMinFeeRate();
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
        case Currency::CurrBtc:  return minFeeRateBtc();
        case Currency::CurrLtc:  return minFeeRateLtc();
        case Currency::CurrQtum: return minFeeRateQtum();
        default: assert(false); return 0;
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
