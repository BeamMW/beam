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

#include "utility/io/reactor.h"
#include "settings_provider.h"

#include <memory>

namespace beam::bitcoin
{
    class IBridgeHolder
    {
    public:
        using Ptr = std::shared_ptr<IBridgeHolder>;

        virtual ~IBridgeHolder() {};

        virtual IBridge::Ptr Get(io::Reactor& reactor, ISettingsProvider& settingsProvider) = 0;
        virtual void Reset() = 0;
    };

    template<typename ElectrumBridge, typename CoreBridge>
    class BridgeHolder : public IBridgeHolder
    {
    public:
        BridgeHolder()
            : m_bridgeConnectionType(ISettings::ConnectionType::None)
        {
        }

        IBridge::Ptr Get(io::Reactor& reactor, ISettingsProvider& settingsProvider) override
        {
            if (settingsProvider.GetSettings().IsCoreActivated())
            {
                if (m_bridgeConnectionType != ISettings::ConnectionType::Core)
                {
                    m_bridge = std::make_shared<CoreBridge>(reactor, settingsProvider);
                    m_bridgeConnectionType = ISettings::ConnectionType::Core;
                }
            }
            else if (settingsProvider.GetSettings().IsElectrumActivated())
            {
                if (m_bridgeConnectionType != ISettings::ConnectionType::Electrum)
                {
                    m_bridge = std::make_shared<ElectrumBridge>(reactor, settingsProvider);
                    m_bridgeConnectionType = ISettings::ConnectionType::Electrum;
                }
            }
            else
            {
                Reset();
            }

            return m_bridge;
        }

        void Reset() override
        {
            m_bridge.reset();
            m_bridgeConnectionType = ISettings::ConnectionType::None;
        }

    private:
        IBridge::Ptr m_bridge;
        ISettings::ConnectionType m_bridgeConnectionType;
    };
}