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

#pragma once

#include <QObject>

#include "ui/model/settings.h"

class NewscastSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool    isExcRatesActive    READ isExcRatesActive    WRITE setExcRatesActive    NOTIFY excRatesActiveChanged)
    Q_PROPERTY(bool    isUpdatesPushActive READ isUpdatesPushActive WRITE setUpdatesPushActive NOTIFY updatesPushActiveChanged)
    Q_PROPERTY(QString publisherKey        READ getPublisherKey     WRITE setPublisherKey      NOTIFY publisherKeyChanged)
    Q_PROPERTY(bool    isSettingsChanged   READ isSettingsChanged                              NOTIFY settingsChanged)

public:
    NewscastSettings(WalletSettings&);

    bool isExcRatesActive();
    void setExcRatesActive(bool);
    bool isUpdatesPushActive();
    void setUpdatesPushActive(bool);
    bool isSettingsChanged();
    QString getPublisherKey();
    void setPublisherKey(QString);

signals:
    void excRatesActiveChanged();
    void updatesPushActiveChanged();
    void publisherKeyChanged();
    void settingsChanged();

public slots:
    void apply();
    void restore();
    
private:
    WalletSettings& m_storage;

    bool m_isExcRatesActive;
    bool m_isUpdatesPushActive;
    QString m_publisherKey;
};
