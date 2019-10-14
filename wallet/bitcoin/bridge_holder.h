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
        enum TypeBridge
        {
            None,
            Electrum,
            Core
        };
    public:
        BridgeHolder(/*io::Reactor& reactor, ISettingsProvider::Ptr settingsProvider*/)
            : /*m_reactor(reactor)
            , m_settingsProvider(settingsProvider)
            , */m_type(None)
        {
        }

        IBridge::Ptr Get(io::Reactor& reactor, ISettingsProvider& settingsProvider) override
        {
            if (settingsProvider.GetBitcoinCoreSettings().IsInitialized() && m_type != Core)
            {
                m_bridge = std::make_shared<CoreBridge>(reactor, settingsProvider);
                m_type = Core;
            }

            if (settingsProvider.GetElectrumSettings().IsInitialized() && m_type != Electrum)
            {
                m_bridge = std::make_shared<ElectrumBridge>(reactor, settingsProvider);
                m_type = Electrum;
            }

            return m_bridge;
        }

        void Reset() override
        {
            m_bridge.reset();
            m_type = None;
        }

    private:
        /*io::Reactor& m_reactor;
        ISettingsProvider::Ptr m_settingsProvider;*/
        IBridge::Ptr m_bridge;
        TypeBridge m_type;
    };
}