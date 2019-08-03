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
#include "model/app_model.h"
#include "wallet/common.h"

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

bool QMLGlobals::isFeeOK(int fee, Currency currency)
{
    switch (currency)
    {
    case Currency::CurrBEAM: return fee >= minFeeBEAM();
    case Currency::CurrBTC:  return fee >= minFeeRateBTC();
    case Currency::CurrLTC:  return fee >= minFeeRateLTC();
    case Currency::CurrQTUM: return fee >= minFeeRateQTUM();
    default:
        assert(false);
        return false;
    }
}

int QMLGlobals::minFeeBEAM()
{
    assert(AppModel::getInstance().getWallet());
    return AppModel::getInstance().getWallet()->isFork1() ? kFeeInGroth_Fork1 : kDefaultFeeInGroth;
}

int QMLGlobals::defFeeBEAM()
{
    return minFeeBEAM();
}

int QMLGlobals::minFeeRateBTC()
{
     const auto btcSettings = AppModel::getInstance().getBitcoinClient()->GetSettings();
     return btcSettings.GetMinFeeRate();
}

int QMLGlobals::defFeeRateBTC()
{
     const auto btcSettings = AppModel::getInstance().getBitcoinClient()->GetSettings();
     return btcSettings.GetFeeRate();
}

int QMLGlobals::minFeeRateLTC()
{
    // TODO:SWAP-SETTINGS read from settings (see btc for an example)
    return 90000;
}

int QMLGlobals::defFeeRateLTC()
{
    // TODO:SWAP-SETTINGS read from settings (see btc for an example)
    return 90000;
}

int QMLGlobals::minFeeRateQTUM()
{
    // TODO:SWAP-SETTINGS read from settings (see btc for an example)
    return 90000;
}

int QMLGlobals::defFeeRateQTUM()
{
    // TODO:SWAP-SETTINGS read from settings (see btc for an example)
    return 90000;
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
        case Currency::CurrBEAM: return minFeeBEAM();
        case Currency::CurrBTC:  return minFeeRateBTC();
        case Currency::CurrLTC:  return minFeeRateLTC();
        case Currency::CurrQTUM: return minFeeRateQTUM();
        default: assert(false); return 0;
    }
}