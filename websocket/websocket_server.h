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

#include "reactor.h"
#include "utility/io/timer.h"
#include "utility/thread.h"

#include <functional>
#include <string>
#include <memory>
#include <thread>
#include <boost/asio/ip/tcp.hpp>
#include <boost/optional.hpp>
#include <boost/asio/ssl.hpp>

namespace beam
{
    class WebSocketServer
    {
    public:
        using SendFunc = std::function<void(const std::string&)>;
        using CloseFunc = std::function<void(std::string&&)>;

        struct ClientHandler
        {
            using Ptr = std::shared_ptr<ClientHandler>;
            virtual void ReactorThread_onWSDataReceived(const std::string&) = 0;
            virtual ~ClientHandler() = default;
        };

        WebSocketServer(SafeReactor::Ptr reactor, uint16_t port, std::string allowedOrigin, boost::asio::ssl::context* tlsContext = nullptr);
        ~WebSocketServer();

    protected:
        virtual ClientHandler::Ptr ReactorThread_onNewWSClient(SendFunc, CloseFunc) = 0;

    private:
        boost::asio::io_context    _ioc;
        std::shared_ptr<MyThread>  _iocThread;
        std::string                _allowedOrigin;
        boost::asio::ssl::context* _tlsContext;
    };
}
