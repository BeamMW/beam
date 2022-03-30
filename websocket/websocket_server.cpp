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
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/filesystem.hpp>

namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>

namespace beam
{
    namespace
    {
        // Accepts incoming connections and launches the sessions
        class Listener : public std::enable_shared_from_this<Listener>
        {
        public:
            Listener(boost::asio::io_context& ioc, tcp::endpoint endpoint, SafeReactor::Ptr reactor, HandlerCreator creator, const std::string& allowedOrigin, ssl::context* tlsContext)
                : m_ioc(ioc)
                , m_acceptor(net::make_strand(ioc))
                , m_reactor(std::move(reactor))
                , m_handlerCreator(std::move(creator))
                , m_allowedOrigin(allowedOrigin)
                , m_tlsContext(tlsContext)
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
                    net::make_strand(m_ioc),
                    beast::bind_front_handler(
                        &Listener::on_accept,
                        shared_from_this()));
            }

            void on_accept(boost::system::error_code ec, tcp::socket socket)
            {
                if (ec)
                {
                    fail(ec, "accept");
                }
                else
                {
                    if (m_allowedOrigin.empty())
                    {
                        // Create the Detect Session and run it
                        std::make_shared<DetectSession>(std::move(socket), m_tlsContext, m_reactor, m_handlerCreator)->run();
                    }
                    else
                    {
                        // Create the HttpSession and run it to handle Origin field
                        std::make_shared<HttpSession>(std::move(socket), m_reactor, m_handlerCreator, m_allowedOrigin)->run();
                    }
                }

                // Accept another connection
                do_accept();
            }

        private:
            boost::asio::io_context& m_ioc;
            tcp::acceptor m_acceptor;
            SafeReactor::Ptr m_reactor;
            HandlerCreator m_handlerCreator;
            std::string m_allowedOrigin;
            ssl::context* m_tlsContext;
        };
    }

    WebSocketServer::WebSocketServer(SafeReactor::Ptr reactor, const Options& options)
        : _ioc(1)
        , _allowedOrigin(std::move(options.allowedOrigin))
       
    {
        if (options.useTls)
        {
            _tlsContext = std::make_unique<ssl::context>(ssl::context::tlsv13_server);
            auto& ctx = *_tlsContext;
            ctx.set_options(
                ssl::context::default_workarounds |
                ssl::context::no_sslv2 |
                ssl::context::no_sslv3 );
            ctx.set_verify_mode(ssl::verify_none);
            if (!options.certificate.empty())
            {
                ctx.use_certificate_chain(
                    boost::asio::buffer(options.certificate.data(), options.certificate.size()));
            }
            else if (!options.certificatePath.empty())
            {
                ctx.use_certificate_chain_file(options.certificatePath);
            }
            if (!options.key.empty())
            {
                ctx.use_private_key(
                    boost::asio::buffer(options.key.data(), options.key.size()),
                    boost::asio::ssl::context::file_format::pem);
            }
            else if (!options.keyPath.empty())
            {
                ctx.use_private_key_file(options.keyPath, ssl::context::file_format::pem);
            }
            if (!options.dhParams.empty())
            {
                ctx.set_options(ssl::context::single_dh_use);
                ctx.use_tmp_dh(boost::asio::buffer(options.dhParams.data(), options.dhParams.size()));
            }
            else if (!options.dhParamsPath.empty() && boost::filesystem::exists(options.dhParamsPath))
            {
                ctx.set_options(ssl::context::single_dh_use);
                ctx.use_tmp_dh_file(options.dhParamsPath);
            }
        }
        LOG_INFO() << "Listening websocket protocol on port " << options.port;
        _iocThread = std::make_shared<MyThread>([this, port = options.port, reactor]()
        {
            HandlerCreator creator = [this, reactor](WebSocketServer::SendFunc func, WebSocketServer::CloseFunc closeFunc) -> auto
            {
                reactor->assert_thread();
                return ReactorThread_onNewWSClient(std::move(func), std::move(closeFunc));
            };

            std::make_shared<Listener>(_ioc,
                tcp::endpoint{ boost::asio::ip::make_address("0.0.0.0"), port },
                reactor, creator, _allowedOrigin, _tlsContext.get())->run();

            _ioc.run();
        });
    }

    WebSocketServer::~WebSocketServer()
    {
        LOG_INFO() << "Stopping websocket server...";
        _ioc.stop();
        if (_iocThread && _iocThread->joinable())
        {
            _iocThread->join();
        }
        LOG_INFO() << "Websocket server stopped";
    }
}
