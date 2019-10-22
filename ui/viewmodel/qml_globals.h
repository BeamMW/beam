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
#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include "currencies.h"

class QMLGlobals : public QObject
{
    Q_OBJECT
public:
    QMLGlobals(QQmlEngine&);

    Q_INVOKABLE static void showMessage(const QString& message);
    Q_INVOKABLE static void copyToClipboard(const QString& text);
    Q_INVOKABLE QString version();
    Q_INVOKABLE static bool isTransactionToken(const QString& text);
    Q_INVOKABLE static bool isSwapToken(const QString& text);
    Q_INVOKABLE static bool isTAValid(const QString& text);
    Q_INVOKABLE static QString getLocaleName();
    Q_INVOKABLE static int  maxCommentLength();
    Q_INVOKABLE static bool needPasswordToSpend();
    Q_INVOKABLE static bool isPasswordValid(const QString& value);

    // Currency utils
    static bool isFeeOK(unsigned int fee, Currency currency);
    static int  getMinFeeOrRate(Currency currency);

    Q_INVOKABLE static unsigned int minFeeBeam();
    Q_INVOKABLE static unsigned int minFeeRateBtc();
    Q_INVOKABLE static unsigned int minFeeRateLtc();
    Q_INVOKABLE static unsigned int minFeeRateQtum();

    Q_INVOKABLE static unsigned int defFeeBeam();
    Q_INVOKABLE static unsigned int defFeeRateBtc();
    Q_INVOKABLE static unsigned int defFeeRateLtc();
    Q_INVOKABLE static unsigned int defFeeRateQtum();

    Q_INVOKABLE static QString beamFeeRateLabel();
    Q_INVOKABLE static QString btcFeeRateLabel();
    Q_INVOKABLE static QString ltcFeeRateLabel();
    Q_INVOKABLE static QString qtumFeeRateLabel();

    // Swap & other currencies utils
    Q_INVOKABLE static bool canSwap();
    Q_INVOKABLE static bool haveBtc();
    Q_INVOKABLE static bool haveLtc();
    Q_INVOKABLE static bool haveQtum();

    Q_INVOKABLE static QString rawTxParametrsToTokenStr(
            QVariant variantTxParams);

private:
    QQmlEngine& _engine;
};
