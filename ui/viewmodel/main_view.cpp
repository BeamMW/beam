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

#include "main_view.h"
#include "model/app_model.h"

namespace
{
    const int msInMinute = 60 * 1000;
    const int LockTimeouts[] =
    {
        0 * msInMinute,
        1 * msInMinute,
        5 * msInMinute,
        15 * msInMinute,
        30 * msInMinute,
        60 * msInMinute,
    };

    const int minResetPeriodInMs = 1000;
}

MainViewModel::MainViewModel()
    : m_settings{AppModel::getInstance().getSettings()}
    , m_timer(this)
{
    m_timer.setSingleShot(true);

    connect(&m_timer, SIGNAL(timeout()), this, SLOT(lockWallet()));
    connect(&m_settings, SIGNAL(lockTimeoutChanged()), this, SLOT(onLockTimeoutChanged()));

#if defined(BEAM_HW_WALLET)
    connect(AppModel::getInstance().getWallet().get(), SIGNAL(showTrezorMessage()), this, SIGNAL(showTrezorMessage()));
    connect(AppModel::getInstance().getWallet().get(), SIGNAL(hideTrezorMessage()), this, SIGNAL(hideTrezorMessage()));
    connect(AppModel::getInstance().getWallet().get(), SIGNAL(showTrezorError(const QString&)), this, SIGNAL(showTrezorError(const QString&)));
#endif

    onLockTimeoutChanged();
}

void MainViewModel::update(int page)
{
	// TODO: update page model or smth...
}

void MainViewModel::lockWallet()
{
    emit gotoStartScreen();
}

void MainViewModel::onLockTimeoutChanged()
{
    int index = m_settings.getLockTimeout();

    assert(static_cast<size_t>(index) < sizeof(LockTimeouts) / sizeof(LockTimeouts[0]));

    if (index > 0)
    {
        m_timer.start(LockTimeouts[index]);
    }
    else
    {
        m_timer.stop();
    }
}

void MainViewModel::resetLockTimer()
{
    if (m_timer.isActive() && (m_timer.interval() - m_timer.remainingTime() > minResetPeriodInMs))
    {
        m_timer.start();
    }
}
