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

#include "hw_wallet.h"
#include "utility/logger.h"
#include "utility/helpers.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#else
#	pragma warning (push, 0)
#   pragma warning( disable : 4996 )
#endif

#include "client.hpp"
#include "queue/working_queue.h"
#include "device_manager.hpp"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (pop)
#endif

#include "trezor_key_keeper.h"

namespace beam::wallet
{
    HWWallet::HWWallet()
        : m_client(std::make_shared<Client>())   
    {

    }

    bool HWWallet::isConnected() const
    {
        return !m_client->enumerate().empty();
    }

    std::vector<std::string> HWWallet::getDevices() const
    {
        auto devices = m_client->enumerate();
        std::vector<std::string> items;

        for (auto device : devices)
        {
            items.push_back(std::to_string(device));
        }

        return items;
    }

    IPrivateKeyKeeper2::Ptr HWWallet::getKeyKeeper(const std::string& deviceName, const IHandler::Ptr& uiHandler)
    {
        return std::make_shared<TrezorKeyKeeperProxy>(std::make_shared<Client>(), deviceName, uiHandler);
    }
}
