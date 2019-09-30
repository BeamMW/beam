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
#include "el_seed_validator.h"

#include "utility/string_helpers.h"
#include "wallet/bitcoin/common.h"

ELSeedValidator::ELSeedValidator(QObject* parent):
    QValidator(parent)
{
}

QValidator::State ELSeedValidator::validate(QString& s, int& pos) const
{
    QRegularExpression re("^([a-z]{2,20}\\ ){11}([a-z]{2,20}){1}$");
    QRegularExpressionMatch match = re.match(s, 0, QRegularExpression::PartialPreferCompleteMatch);

    if (match.hasMatch()) {
        auto secretWords = string_helpers::split(s.toStdString(), ' ');

        if (beam::bitcoin::validateElectrumMnemonic(secretWords))
        {
            return Acceptable;
        }
        return Intermediate;
    }
    else if (s.isEmpty() || match.hasPartialMatch()) 
    {
        return Intermediate;
    }
    else
    {
        return Invalid;
    }
}