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

#include "model/settings.h"

class NotificationsSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool    isNewVersionActive  READ isNewVersionActive  WRITE setNewVersionActive  NOTIFY newVersionActiveChanged)
    Q_PROPERTY(bool    isBeamNewsActive    READ isBeamNewsActive    WRITE setBeamNewsActive    NOTIFY beamNewsActiveChanged)
    Q_PROPERTY(bool    isTxStatusActive    READ isTxStatusActive    WRITE setTxStatusActive    NOTIFY txStatusActiveChanged)

public:
    NotificationsSettings(WalletSettings&);

    bool isNewVersionActive();
    bool isBeamNewsActive();
    bool isTxStatusActive();

    void setNewVersionActive(bool);
    void setBeamNewsActive(bool);
    void setTxStatusActive(bool);

signals:
    void newVersionActiveChanged();
    void beamNewsActiveChanged();
    void txStatusActiveChanged();

public slots:
    void loadFromStorage();
    
private:
    WalletSettings& m_storage;

    bool m_isNewVersionActive;
    bool m_isBeamNewsActive;
    bool m_isTxStatusActive;
};
