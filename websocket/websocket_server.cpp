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
#include "websocket_server.h"
#include "sessions.h"
#include "utility/logger.h"
#include "reactor.h"

namespace beam::wallet
{
    namespace
    {
        // Accepts incoming connections and launches the sessions
        class Listener : public std::enable_shared_from_this<Listener>
        {
        public:
            Listener(boost::asio::io_context& ioc, tcp::endpoint endpoint, SafeReactor::Ptr reactor, HandlerCreator creator, const std::string& allowedOrigin)
                : m_acceptor(ioc)
                , m_socket(ioc)
                , m_reactor(std::move(reactor))
                , m_handlerCreator(std::move(creator))
                , m_allowedOrigin(allowedOrigin)
            {
                boost::system::error_code ec;

                // Open the acceptor
                m_acceptor.open(endpoint.protocol(), ec);
                if (ec)
                {
                    failEx(ec, "open");
                }

                // Allow address reuse
                m_acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
                if (ec)
                {
                    failEx(ec, "set_option");
                    throw std::runtime_error(ec.message());
                }

                // Bind to the server address
                m_acceptor.bind(endpoint, ec);
                if (ec)
                {
                    failEx(ec, "bind");
                }

                // Start listening for connections
                m_acceptor.listen(
                    boost::asio::socket_base::max_listen_connections, ec);

                if (ec)
                {
                    failEx(ec, "listen");
                }
            }

            // Start accepting incoming connections
            void run()
            {
                if (!m_acceptor.is_open())
                    return;
                do_accept();
            }

            void do_accept()
            {
                m_acceptor.async_accept(
                    m_socket,
                    [sp = shared_from_this()](boost::system::error_code ec)
                {
                    sp->on_accept(ec);
                });
            }

            void on_accept(boost::system::error_code ec)
            {
                if (ec)
                {
                    fail(ec, "accept");
                }
                else
                {
                    if (m_allowedOrigin.empty())
                    {
                        // Create the Session and run it
                        std::make_shared<WebsocketSession>(std::move(m_socket), m_reactor, m_handlerCreator)->run();
                    }
                    else
                    {
                        // Create the HttpSession and run it to handle Origin field
                        std::make_shared<HttpSession>(std::move(m_socket), m_reactor, m_handlerCreator, m_allowedOrigin)->run();
                    }
                }

                // Accept another connection
                do_accept();
            }

        private:
            tcp::acceptor m_acceptor;
            tcp::socket m_socket;
            SafeReactor::Ptr m_reactor;
            HandlerCreator m_handlerCreator;
            std::string m_allowedOrigin;
        };
    }

    WebSocketServer::WebSocketServer(SafeReactor::Ptr reactor, uint16_t port, std::string allowedOrigin)
        : _ioc(1)
        , _allowedOrigin(std::move(allowedOrigin))
    {
        _iocThread = std::make_shared<MyThread>([this, port, reactor]() {

            HandlerCreator creator = [this, reactor] (WebSocketServer::SendFunc func, WebSocketServer::CloseFunc closeFunc) -> auto {
                reactor->assert_thread();
                return ReactorThread_onNewWSClient(std::move(func), std::move(closeFunc));
            };

            std::make_shared<Listener>(_ioc,
                    tcp::endpoint{ boost::asio::ip::make_address("0.0.0.0"), port },
                    reactor, creator, _allowedOrigin)->run();

            _ioc.run();
        });
    }

    WebSocketServer::~WebSocketServer()
    {
        _ioc.stop();
        if (_iocThread && _iocThread->joinable())
        {
            _iocThread->join();
        }
    }
}
