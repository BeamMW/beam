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

namespace beam
{
    HWWallet::HWWallet()
    {
        Client client;
        std::vector<std::unique_ptr<DeviceManager>> trezors;
        std::vector<std::unique_ptr<std::atomic_flag>> is_alive_flags;

        auto enumerates = client.enumerate();

        if (enumerates.size() == 0)
        {
            //LOG_DEBUG() << "there is no device connected";
        }

        auto clear_flag = [&](size_t queue_size, size_t idx) {
            if (queue_size == 0)
                is_alive_flags.at(idx)->clear();
        };

        for (auto enumerate : enumerates)
        {
            trezors.push_back(std::unique_ptr<DeviceManager>(new DeviceManager()));
            auto af = std::unique_ptr<std::atomic_flag>(new std::atomic_flag());
            af->test_and_set();
            is_alive_flags.emplace_back(move(af));
            auto is_alive_idx = is_alive_flags.size() - 1;
            auto& trezor = trezors.back();

            if (enumerate.session != "null")
            {
                client.release(enumerate.session);
                enumerate.session = "null";
            }

            trezor->callback_Failure([&, is_alive_idx](const Message &msg, size_t queue_size) {
                std::cout << "FAIL REASON: " << child_cast<Message, Failure>(msg).message() << std::endl;
                clear_flag(queue_size, is_alive_idx);
            });

            trezor->callback_Success([&, is_alive_idx](const Message &msg, size_t queue_size) {
                std::cout << "SUCCESS: " << child_cast<Message, Success>(msg).message() << std::endl;
                clear_flag(queue_size, is_alive_idx);
            });

            try
            {
                trezor->init(enumerate);
                trezor->call_Ping("hello beam", true);
                trezor->call_BeamGetOwnerKey(true, [&, is_alive_idx](const Message &msg, size_t queue_size) {
                    std::cout << "BEAM OWNER KEY: " << child_cast<Message, BeamOwnerKey>(msg).key() << std::endl;
                    clear_flag(queue_size, is_alive_idx);
                });
                //trezor->call_BeamGenerateNonce(1, [&, is_alive_idx](const Message &msg, size_t queue_size) {
                //    std::cout << "BEAM NONCE IN SLOT 1: ";
                //    print_bin(reinterpret_cast<const uint8_t*>(child_cast<Message, BeamECCImage>(msg).image_x().c_str()), 32);
                //    clear_flag(queue_size, is_alive_idx);
                //});
            }
            catch (std::runtime_error e)
            {
                std::cout << e.what() << std::endl;
            }
        }

        for (auto& is_alive : is_alive_flags)
            while (is_alive->test_and_set())
                ; // waiting
        curl_global_cleanup();
    }

    std::string HWWallet::getOwnerKey() const
    {
        return "owner key";
    }
}
