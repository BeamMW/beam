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

#ifdef BEAM_IPFS_SUPPORT
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <stdexcept>

namespace beam::wallet
{
    struct IPFSService
    {
        struct Handler {
            virtual ~Handler() = default;
            // execute callback in the same thread where ::create has been called
            virtual void pushToClient(std::function<void()>&& action) = 0;
        };

        typedef std::shared_ptr<IPFSService> Ptr;
        typedef std::shared_ptr<Handler> HandlerPtr;
        typedef std::function<void (std::string&&)> Err;
        virtual ~IPFSService() = default;

        [[nodiscard]] static Ptr create(HandlerPtr);

        /// \exception std::runtime_error
        virtual void start(const std::string& storagePath) = 0;

        /// \exception std::runtime_error
        virtual void stop() = 0;

        /// \result IPFS node ID
        [[nodiscard]] virtual std::string id() const = 0;

        /// \brief Store data in IPFS and get ipfs hash back via callback.
        ///        Async, executed in other thread, may take long time or fail.
        virtual void add(std::vector<uint8_t>&& data,
                         std::function<void (std::string&&)>&& res,
                         Err&&) = 0;

        /// \brief Get data from IPFS
        virtual void get(const std::string& hash, uint32_t timeout,
                         std::function<void (std::vector<uint8_t>&&)>&& res,
                         Err&&) = 0;

        /// \brief Pin to the local node
        virtual void pin(const std::string& hash, uint32_t timeout,
                         std::function<void ()>&& res,
                         Err&&) = 0;
    };
}
#endif