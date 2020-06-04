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
#include <functional>
#include <string>
#include <memory>

namespace beam::wallet
{
    class WebSocketServer
    {
    public:

        class IHandler
        {
        public:
            using Ptr = std::unique_ptr<IHandler>;
            virtual ~IHandler() {};
            virtual void processData(const std::string&) {};
        };

        using SendMessageFunc = std::function<void(const std::string&)>;
        using HandlerCreator = std::function<IHandler::Ptr (SendMessageFunc&&)>;
        using StartAction = std::function<void()>;

        WebSocketServer(beam::io::Reactor::Ptr reactor, uint16_t port,
            HandlerCreator&& creator, StartAction&& startAction = {}, const std::string& allowedOrigin = "");
        ~WebSocketServer();

    private:
        struct WebSocketServerImpl;
        std::unique_ptr<WebSocketServerImpl> m_impl;
    };
}

