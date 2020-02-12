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

#include "news_settings.h"

NewscastSettings::NewscastSettings(WalletSettings& walletSettings)
    : m_storage(walletSettings)
{
    restore();
}

bool NewscastSettings::isExcRatesActive()
{
    return m_isExcRatesActive;
}

void NewscastSettings::setExcRatesActive(bool isActive)
{
    if (isActive != m_isExcRatesActive)
    {
        m_isExcRatesActive = isActive;
        emit excRatesActiveChanged();
        emit settingsChanged();
    }
}

bool NewscastSettings::isUpdatesPushActive()
{
    return m_isUpdatesPushActive;
}

void NewscastSettings::setUpdatesPushActive(bool isActive)
{
    if (isActive != m_isUpdatesPushActive)
    {
        m_isUpdatesPushActive = isActive;
        emit updatesPushActiveChanged();
        emit settingsChanged();
    }
}

bool NewscastSettings::isSettingsChanged()
{
    return  m_publisherKey != m_storage.getNewscastKey() ||
            m_isUpdatesPushActive != m_storage.isUpdatesPushActive() ||
            m_isExcRatesActive != m_storage.isExcRatesActive();
}

QString NewscastSettings::getPublisherKey()
{
    return m_publisherKey;
}

void NewscastSettings::setPublisherKey(QString key)
{
    if (key != m_publisherKey)
    {
        m_publisherKey = key;
        emit publisherKeyChanged();
        emit settingsChanged();
    }
}

void NewscastSettings::apply()
{
    m_storage.setNewscastKey(m_publisherKey);
    m_storage.setUpdatesPushActive(m_isUpdatesPushActive);
    m_storage.setExcRatesActive(m_isExcRatesActive);
    emit settingsChanged();
}

void NewscastSettings::restore()
{
    m_publisherKey = m_storage.getNewscastKey();
    m_isUpdatesPushActive = m_storage.isUpdatesPushActive();
    m_isExcRatesActive = m_storage.isExcRatesActive();
    emit excRatesActiveChanged();
    emit updatesPushActiveChanged();
    emit publisherKeyChanged();
    emit settingsChanged();
}
