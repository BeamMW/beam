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

#include "utility/io/reactor.h"
#include "settings_provider.h"
#include "ethereum_bridge.h"

#include <memory>

namespace beam::ethereum
{
    class IBridgeHolder
    {
    public:
        using Ptr = std::shared_ptr<IBridgeHolder>;

        virtual ~IBridgeHolder() {};

        virtual IBridge::Ptr Get(io::Reactor& reactor, ISettingsProvider& settingsProvider) = 0;
        virtual void Reset() = 0;
    };

    class BridgeHolder : public IBridgeHolder
    {
    public:
        BridgeHolder()
        {
        }

        IBridge::Ptr Get(io::Reactor& currentReactor, ISettingsProvider& settingsProvider) override
        {
            if (settingsProvider.GetSettings().IsActivated())
            {
                if (!m_bridge)
                {
                    m_bridge = std::make_shared<EthereumBridge>(currentReactor, settingsProvider);
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
        }

    private:
        IBridge::Ptr m_bridge;
    };
}