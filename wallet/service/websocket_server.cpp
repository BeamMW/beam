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
#include <boost/beast/http.hpp>

#include <thread>
#include <mutex>
#include <queue>

using namespace beam;
using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>

namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>
namespace beast = boost::beast;                 // from <boost/beast.hpp>
namespace http = beast::http;                   // from <boost/beast/http.hpp>

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

            template<class Body, class Allocator>
            void do_accept(http::request<Body, http::basic_fields<Allocator>> req)
            {
                m_webSocket.async_accept(
                    req,
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
                while (true)
                {
                    std::string data;
                    {
                        std::unique_lock<std::mutex> lock(m_queueMutex);
                        if (m_dataQueue.empty())
                            return;
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
            };

        // Handles an HTTP server connection, we use it to filter origin
        class HttpSession : public std::enable_shared_from_this<HttpSession>
        {
            // This queue is used for HTTP pipelining.
            class Queue
            {
                enum
                {
                    // Maximum number of responses we will queue
                    limit = 8
                };

                // The type-erased, saved work item
                struct Work
                {
                    virtual ~Work() = default;
                    virtual void operator()() = 0;
                };

                HttpSession& m_self;
                std::vector<std::unique_ptr<Work>> m_items;

            public:
                explicit
                    Queue(HttpSession& self)
                    : m_self(self)
                {
                    static_assert(limit > 0, "queue limit must be positive");
                    m_items.reserve(limit);
                }

                // Returns `true` if we have reached the queue limit
                bool is_full() const
                {
                    return m_items.size() >= limit;
                }

                // Called when a message finishes sending
                // Returns `true` if the caller should initiate a read
                bool on_write()
                {
                    BOOST_ASSERT(!m_items.empty());
                    auto const was_full = is_full();
                    m_items.erase(m_items.begin());
                    if (!m_items.empty())
                        (*m_items.front())();
                    return was_full;
                }

                // Called by the HTTP handler to send a response.
                template<bool isRequest, class Body, class Fields>
                void operator()(http::message<isRequest, Body, Fields>&& msg)
                {
                    // This holds a work item
                    struct WorkImpl : Work
                    {
                        HttpSession& m_self;
                        http::message<isRequest, Body, Fields> m_msg;

                        WorkImpl(
                            HttpSession& self,
                            http::message<isRequest, Body, Fields>&& msg)
                            : m_self(self)
                            , m_msg(std::move(msg))
                        {
                        }

                        void operator()()
                        {
                            http::async_write(
                                m_self.m_socket,
                                m_msg,
                                [sp = m_self.shared_from_this(), close = m_msg.need_eof()](beast::error_code ec, std::size_t bytes_transferred)
                            {
                                sp->on_write(close, ec, bytes_transferred);
                            });
                        }
                    };

                    // Allocate and store the work
                    m_items.push_back(
                        boost::make_unique<WorkImpl>(m_self, std::move(msg)));

                    // If there was no previous work, start this one
                    if (m_items.size() == 1)
                        (*m_items.front())();
                }
            };

            tcp::socket m_socket;
            beast::flat_buffer m_buffer;
            http::request<http::string_body> m_request;

            Queue m_queue;
            io::Reactor::Ptr m_reactor;
            WebSocketServer::HandlerCreator& m_handlerCreator;
            std::string m_allowedOrigin;

        public:
            // Take ownership of the socket
            HttpSession(tcp::socket&& socket, io::Reactor::Ptr reactor, WebSocketServer::HandlerCreator& creator, const std::string& allowedOrigin)
                : m_socket(std::move(socket))
                , m_queue(*this)
                , m_reactor(reactor)
                , m_handlerCreator(creator)
                , m_allowedOrigin(allowedOrigin)
            {
            }

            // Start the session
            void run()
            {
                do_read();
            }

        private:
            void do_read()
            {
                // Read a request using the parser-oriented interface
                http::async_read(
                    m_socket,
                    m_buffer,
                    m_request,
                    [sp = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred)
                {
                    sp->on_read(ec, bytes_transferred);
                });
            }

            void on_read(beast::error_code ec, std::size_t bytes_transferred)
            {
                boost::ignore_unused(bytes_transferred);

                // This means they closed the connection
                if (ec == http::error::end_of_stream)
                    return do_close();

                if (ec)
                    return fail(ec, "read");

                {
                    auto const forbiddenRequest =
                        [this](beast::string_view why)
                    {
                        http::response<http::string_body> res;
                        res.version(m_request.version());
                        res.result(http::status::forbidden);
                        res.body() = std::string(why);
                        res.prepare_payload();
                        return res;
                    };
                    {
                        if (!beast::iequals(m_request[http::field::origin], m_allowedOrigin))
                        {
                            m_queue(forbiddenRequest("origin is not allowed"));
                            return;
                        }
                    }
                }

                // Create a websocket session, transferring ownership
                // of both the socket and the HTTP request.
                std::make_shared<Session>(std::move(m_socket), m_reactor, m_handlerCreator)->do_accept(std::move(m_request));
                return;
            }

            void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
            {
                boost::ignore_unused(bytes_transferred);

                if (ec)
                    return fail(ec, "write");

                if (close)
                {
                    // This means we should close the connection, usually because
                    // the response indicated the "Connection: close" semantic.
                    return do_close();
                }

                // Inform the queue that a write completed
                if (m_queue.on_write())
                {
                    // Read another request
                    do_read();
                }
            }

            void do_close()
            {
                // Send a TCP shutdown
                beast::error_code ec;
                m_socket.shutdown(tcp::socket::shutdown_send, ec);

                // At this point the connection is closed gracefully
            }
        };

        // Accepts incoming connections and launches the sessions
        class Listener : public std::enable_shared_from_this<Listener>
        {
        public:
            Listener(boost::asio::io_context& ioc, tcp::endpoint endpoint, io::Reactor::Ptr reactor, WebSocketServer::HandlerCreator& creator, const std::string& allowedOrigin)
                : m_acceptor(ioc)
                , m_socket(ioc)
                , m_reactor(reactor)
                , m_handlerCreator(creator)
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
                        std::make_shared<Session>(std::move(m_socket), m_reactor, m_handlerCreator)->run();
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
            io::Reactor::Ptr m_reactor;
            WebSocketServer::HandlerCreator& m_handlerCreator;
            std::string m_allowedOrigin;
        };
        }

    struct WebSocketServer::WebSocketServerImpl
    {
        WebSocketServerImpl(io::Reactor::Ptr reactor, uint16_t port, HandlerCreator&& creator, StartAction&& startAction, const std::string& allowedOrigin)
            : m_ioc{ 1 }
            , m_reactor(reactor)
            , m_handlerCreator(std::move(creator))
            , m_startAction(startAction)
            , m_allowedOrigin(allowedOrigin)
        {
            start(port);
        }

        ~WebSocketServerImpl()
        {
            stop();
            if (m_iocThread && m_iocThread->joinable())
            {
                m_iocThread->join();
            }
        }

        void start(uint16_t port)
        {
            m_iocThread = std::make_shared<std::thread>([this](auto port) {iocThreadFunc(port); }, port);
        }

        void iocThreadFunc(uint16_t port)
        {
            std::make_shared<Listener>(m_ioc, tcp::endpoint{ boost::asio::ip::make_address("0.0.0.0"), port }, m_reactor, m_handlerCreator, m_allowedOrigin)->run();
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
        std::string m_allowedOrigin;
    };

    WebSocketServer::WebSocketServer(io::Reactor::Ptr reactor, uint16_t port, HandlerCreator&& creator, StartAction&& startAction, const std::string& allowedOrigin)
        : m_impl(std::make_unique<WebSocketServerImpl>(reactor, port, std::move(creator), std::move(startAction), allowedOrigin))
    {
    }

    WebSocketServer::~WebSocketServer()
    {
    }
    }