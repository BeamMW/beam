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

ELSeedValidator::ELSeedValidator(QObject* parent):
    QValidator(parent)
{
}

QValidator::State ELSeedValidator::validate(QString& s, int& pos) const
{
    if (s.length() == 0) return QValidator::State::Intermediate;
    if (s.contains('a')) return QValidator::State::Invalid;
    return QValidator::State::Acceptable;
}