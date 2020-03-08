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

#include "utility/logger.h"
#include "utility/io/asyncevent.h"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <thread>
#include <mutex>
#include <queue>

using namespace beam;
using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>

namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

namespace beam::wallet
{
    namespace
    {
        void fail(boost::system::error_code ec, char const* what)
        {
            LOG_ERROR() << what << ": " << ec.message();
        }

        void failEx(boost::system::error_code ec, char const* what)
        {
            fail(ec, what);
            throw std::runtime_error(ec.message());
        }

        

        class Session : public std::enable_shared_from_this<Session>
        {
        public:
            // Take ownership of the socket
            explicit Session(tcp::socket socket, io::Reactor::Ptr reactor, WebSocketServer::HandlerCreator& creator)
                : m_webSocket(std::move(socket))
                , m_newDataEvent(io::AsyncEvent::create(*reactor, [this]() { process_new_data(); }))
                , m_handler(creator([this](const auto& msg) { do_write(msg); }))
                
            {

            }

            ~Session()
            {
                LOG_DEBUG() << "Session destroyed.";
            }

            // Start the asynchronous operation
            void run()
            {
                // Accept the websocket handshake
                m_webSocket.async_accept(
                    [sp = shared_from_this()](boost::system::error_code ec)
                {
                    sp->on_accept(ec);
                });
            }

            void on_accept(boost::system::error_code ec)
            {
                if (ec)
                    return fail(ec, "accept");

                // Read a message
                do_read();
            }

            void do_read()
            {
                // Read a message into our buffer
                m_webSocket.async_read(
                    m_buffer,
                    [sp = shared_from_this()](boost::system::error_code ec, std::size_t bytes)
                {
                    sp->on_read(ec, bytes);
                });
            }

            void on_read(
                boost::system::error_code ec,
                std::size_t bytes_transferred)
            {
                boost::ignore_unused(bytes_transferred);

                // This indicates that the Session was closed
                if (ec == websocket::error::closed)
                    return;

                if (ec)
                    return fail(ec, "read");

                {
                    std::ostringstream os;

# if (BOOST_VERSION/100 % 1000) >= 70
                    os << boost::beast::make_printable(m_buffer.data());
# else
                    os << boost::beast::buffers(m_buffer.data());
# endif
                    m_buffer.consume(m_buffer.size());
                    auto data = os.str();

                    if (data.size())
                    {
                        // LOG_DEBUG() << "data from a client:" << data;

                        process_data_async(std::move(data));
                    }
                }

                do_read();
            }

            void on_write(
                boost::system::error_code ec,
                std::size_t bytes_transferred)
            {
                boost::ignore_unused(bytes_transferred);

                if (ec)
                    return fail(ec, "write");

                std::string* contents = nullptr;
                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    m_writeQueue.pop();
                    if (!m_writeQueue.empty())
                    {
                        contents = &m_writeQueue.front();
                    }
                }

                if (contents)
                {
                    m_webSocket.async_write(
                        boost::asio::buffer(*contents),
                        [sp = shared_from_this()](boost::system::error_code ec, std::size_t bytes)
                    {
                        sp->on_write(ec, bytes);
                    });
                }
            }

            void do_write(const std::string& msg)
            {
                std::string* contents = nullptr;
                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    
                    m_writeQueue.push(msg);
            
                    if (m_writeQueue.size() > 1)
                        return;
            
                    contents = &m_writeQueue.front();
                }
            
                m_webSocket.async_write(
                    boost::asio::buffer(*contents),
                    [sp = shared_from_this()](boost::system::error_code ec, std::size_t bytes)
                {
                    sp->on_write(ec, bytes);
                });
            }

            void process_new_data()
            {
                while (!m_dataQueue.empty())
                {
                    std::string data;
                    {
                        std::unique_lock<std::mutex> lock(m_queueMutex);
                        data = m_dataQueue.front();
                        m_dataQueue.pop();
                    }

                    process_data(data);
                }
            }

            void process_data_async(std::string&& data)
            {
                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    m_dataQueue.push(std::move(data));
                }
                m_newDataEvent->post();
            }

            void process_data(const std::string& data)
            {
                m_handler->processData(data);
            }

        private:
            websocket::stream<tcp::socket> m_webSocket;
            boost::beast::multi_buffer m_buffer;
            io::AsyncEvent::Ptr m_newDataEvent;
            WebSocketServer::IHandler::Ptr m_handler;

            std::mutex m_queueMutex;
            std::queue<std::string> m_dataQueue;
            std::queue<std::string> m_writeQueue;

            //std::queue<KeyKeeperFunc> m_keeperCallbacks;
        };

        // Accepts incoming connections and launches the sessions
        class Listener : public std::enable_shared_from_this<Listener>
        {
        public:
            Listener(boost::asio::io_context& ioc, tcp::endpoint endpoint, io::Reactor::Ptr reactor, WebSocketServer::HandlerCreator& creator)
                : m_acceptor(ioc)
                , m_socket(ioc)
                , m_reactor(reactor)
                , m_handlerCreator(creator)
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
                    // Create the Session and run it
                    auto s = std::make_shared<Session>(std::move(m_socket), m_reactor, m_handlerCreator);
                    s->run();
                }

                // Accept another connection
                do_accept();
            }

        private:
            tcp::acceptor m_acceptor;
            tcp::socket m_socket;
            io::Reactor::Ptr m_reactor;
            WebSocketServer::HandlerCreator& m_handlerCreator;
        };
    }

    struct WebSocketServer::WebSocketServerImpl
    {
        WebSocketServerImpl(io::Reactor::Ptr reactor, uint16_t port, HandlerCreator&& creator, StartAction&& startAction)
            : m_ioc{ 1 }
            , m_reactor(reactor)
            , m_handlerCreator(std::move(creator))
            , m_startAction(startAction)
        {
            start(port);
        }

        ~WebSocketServerImpl()
        {
            stop();
        }

        void start(uint16_t port)
        {
            m_iocThread = std::make_shared<std::thread>([this](auto port) {iocThreadFunc(port); }, port);
        }

        void iocThreadFunc(uint16_t port)
        {
            std::make_shared<Listener>(m_ioc, tcp::endpoint{ boost::asio::ip::make_address("0.0.0.0"), port }, m_reactor, m_handlerCreator)->run();
            if (m_startAction)
            {
                m_startAction();
            }
            m_ioc.run();
        }

        void stop()
        {
            m_ioc.stop();
        }

        boost::asio::io_context m_ioc;
        std::shared_ptr<std::thread> m_iocThread;
        io::Reactor::Ptr m_reactor;
        HandlerCreator m_handlerCreator;
        StartAction m_startAction;
    };

    WebSocketServer::WebSocketServer(io::Reactor::Ptr reactor, uint16_t port, HandlerCreator&& creator, StartAction&& startAction)
        : m_impl(std::make_unique<WebSocketServerImpl>(reactor, port, std::move(creator), std::move(startAction)))
    {
    }

    WebSocketServer::~WebSocketServer()
    {
    }
}