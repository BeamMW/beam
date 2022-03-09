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
#include <asio-ipfs/include/ipfs_config.h>

namespace beam::wallet
{
    struct IPFSService
    {
        struct Handler {
            virtual ~Handler() = default;
            // All IPFS actions are executed in a separate IPFS thread
            // This function is called from that thread and should push
            // the 'action' to the thread where add/get/... is called
            virtual void AnyThread_pushToClient(std::function<void()>&& action) = 0;
            virtual void AnyThread_onStatus(const std::string& error, uint32_t peercnt) = 0;
        };

        typedef std::shared_ptr<IPFSService> Ptr;
        typedef std::shared_ptr<Handler> HandlerPtr;
        typedef std::function<void (std::string&&)> Err;
        virtual ~IPFSService() = default;

        [[nodiscard]] static Ptr AnyThread_create(HandlerPtr);

        /// \exception std::runtime_error
        /// \breief Calling thread becomes the `ServiceThread`
        // TODO:IPFS handle exceptions
        virtual void ServiceThread_start(asio_ipfs::config config) = 0;

        /// \exception std::runtime_error
        virtual void ServiceThread_stop() = 0;

        // \result true if service is active
        [[nodiscard]] virtual bool AnyThread_running() const = 0;

        /// \result IPFS node ID
        [[nodiscard]] virtual std::string AnyThread_id() const = 0;

        /// \brief Store data in IPFS and get ipfs hash back via callback.
        ///        Async, executed in other thread, may take long time or fail.
        virtual void AnyThread_add(std::vector<uint8_t>&& data, bool pin, uint32_t timeout,
                         std::function<void (std::string&&)>&& res,
                         Err&&) = 0;

        /// \brief Calculate IPFS hash (CID) for data back via callback.
        ///        Async, executed in other thread, may take long time or fail.
        virtual void AnyThread_hash(std::vector<uint8_t>&& data, uint32_t timeout,
                                    std::function<void (std::string&&)>&& res,
                                    Err&&) = 0;

        /// \brief Get data from IPFS
        virtual void AnyThread_get(const std::string& hash, uint32_t timeout,
                         std::function<void (std::vector<uint8_t>&&)>&& res,
                         Err&&) = 0;

        /// \brief Pin to the local node
        virtual void AnyThread_pin(const std::string& hash, uint32_t timeout,
                         std::function<void ()>&& res,
                         Err&&) = 0;

        /// \brief Unpin from the local node
        virtual void AnyThread_unpin(const std::string& hash,
                           std::function<void ()>&& res,
                           Err&&) = 0;

        /// \brief GC, i.e. remove all unpinned
        virtual void AnyThread_gc(uint32_t timeout,
                        std::function<void ()>&& res,
                        Err&&) = 0;
    };
}
#endif