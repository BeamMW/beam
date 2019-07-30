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

    // TODO:SWAP check if correct
    return static_cast<bool>(text.toUtf8()[0] & 0x80);
}

bool QMLGlobals::isSwapToken(const QString& text)
{
    // TODO:SWAP This is for tests, implement real check
    return text == "112233";

    // if (!QMLGlobals::isTransactionToken(text)) return false;
    // return
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
    case Currency::CurrBTC:  return fee >= minFeeBTC();
    case Currency::CurrLTC:  return fee >= minFeeLTC();
    case Currency::CurrQTUM: return fee >= minFeeQTUM();
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

int QMLGlobals::minFeeBTC()
{
    return 50000;
}

int QMLGlobals::minFeeLTC()
{
    return 90000;
}

int QMLGlobals::minFeeQTUM()
{
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