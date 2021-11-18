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

#include "ipfs.h"
#include "ipfs_async.h"
#include "asio-ipfs/include/asio_ipfs/node.h"

namespace beam::wallet::imp
{
    class IPFSService: public beam::wallet::IPFSService
    {
    public:
        using HandlerPtr = beam::wallet::IPFSService::HandlerPtr;

        explicit IPFSService(HandlerPtr);
        ~IPFSService() override;

        void start(const std::string& storagePath) override;
        void stop() override;
        void add(std::vector<uint8_t>&& data, std::function<void (std::string&&)>&& res, Err&&) override;
        void get(const std::string& hash, std::function<void (std::vector<uint8_t>&&)>&& res, Err&&) override;

        [[nodiscard]] std::string id() const override
        {
            return _myid;
        }

    private:
        // do not change this to varargs & bind
        // a bit verbose to call but caller would always
        // copy params to lambda if any present and
        // won't pass local vars by accident
        void retToClient(std::function<void ()>&& what);

        std::string _path;
        std::string _myid;
        std::unique_ptr<asio_ipfs::node> _node;

        //
        // Threading & async stuff
        //
        HandlerPtr _handler;
        std::unique_ptr<MyThread> _thread;
        std::unique_ptr<boost::asio::io_context> _ios;

        typedef boost::asio::executor_work_guard<decltype(_ios->get_executor())> IOSGuard;
        std::unique_ptr<IOSGuard> _ios_guard;
    };
}
