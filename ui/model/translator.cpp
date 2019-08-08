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
#include "translator.h"
#include <QApplication>

namespace
{
    const char* kDefaultTranslationsPath = ":/translations";
}

Translator::Translator(WalletSettings& settings, QQmlEngine& engine)
    : _settings(settings)
    , _engine(engine)
{
    loadTranslation();
    connect(&_settings, &WalletSettings::localeChanged, this, &Translator::onLocaleChanged);
}

Translator::~Translator()
{
    qApp->removeTranslator(&_translator);
}

void Translator::loadTranslation()
{
    auto locale = _settings.getLocale();
    if (_translator.load(locale, kDefaultTranslationsPath))
    {
        qApp->installTranslator(&_translator);
        return;
    }

    LOG_WARNING() << "Can't load translation from " << kDefaultTranslationsPath;
}

void Translator::onLocaleChanged()
{
    qApp->removeTranslator(&_translator);
    loadTranslation();
    _engine.retranslate();
}
