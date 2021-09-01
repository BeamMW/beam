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

#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include "websocket_server.h"
#include "utility/io/reactor.h"
#include "utility/io/asyncevent.h"
#include "utility/logger.h"

namespace beam
{
    namespace beast     = boost::beast;
    namespace http      = boost::beast::http;
    namespace websocket = boost::beast::websocket;
    namespace ssl       = boost::asio::ssl;

    void fail(boost::system::error_code ec, char const* what);
    void failEx(boost::system::error_code ec, char const* what);

    using tcp = boost::asio::ip::tcp;
    using HandlerCreator = std::function<WebSocketServer::ClientHandler::Ptr (WebSocketServer::SendFunc, WebSocketServer::CloseFunc)>;

    template<typename Derived>
    class WebsocketSession
    {
    public:
        // Take ownership of the socket
        explicit WebsocketSession(boost::beast::multi_buffer&& buffer, SafeReactor::Ptr reactor, HandlerCreator creator)
            : _buffer(buffer)
            , _reactor(std::move(reactor))
            , _creator(std::move(creator))
        {
            LOG_DEBUG() << "WebsocketSession created";
        }

        ~WebsocketSession()
        {
            LOG_DEBUG() << "WebsocketSession destroyed";

            // Client handler must be destroyed in the Loop thread
            // Transfer ownership and register destroy request
            _reactor->callAsync([handler = std::move(_handler)]() mutable
            {
                handler.reset();
            });
        }

        auto& GetBuffer()
        {
            return _buffer;
        }

        // Start the asynchronous operation
        void run()
        {
            GetDerived().GetStream().binary(true);
            // Set suggested timeout settings for the websocket
            GetDerived().GetStream().set_option(
                websocket::stream_base::timeout::suggested(
                    beast::role_type::server));

            // Accept the websocket handshake
            GetDerived().GetStream().async_accept(
                GetBuffer().data(),
                [sp = GetDerived().shared_from_this()](boost::system::error_code ec)
            {
                sp->GetBuffer().clear();
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

    private:

        Derived& GetDerived()
        {
            return static_cast<Derived&>(*this);
        }

        void process_data_async(std::string&& data)
        {
            //
            // We do not use shared_ptr here cause
            // 1) there will be deadlock, session would be never destroyed and kept by reactor forever
            // 2) session is destroyed in case of error/socket close. There will be no chance to send
            //    any response in this case anyway
            //
            std::weak_ptr<WebsocketSession> wp = GetDerived().shared_from_this();
            _reactor->callAsync([data, creator = _creator, wp]()
            {
                if (auto sp = wp.lock())
                {
                    if (!sp->_handler)
                    {
                        // Client handler must be created in the Loop thread
                        // It is safe to do without any locks cause
                        // all these callbacks would be executed sequentially
                        // in the context of the same thread. So if one creates handler
                        // the next would discover it and skip.
                        // There would be no race conditions as well
                        sp->_handler = creator([wp](const std::string& data) { // SendFunc
                            if (auto sp = wp.lock())
                            {
                                boost::asio::post(
                                    sp->GetDerived().GetStream().get_executor(),
                                    [sp, data]() {
                                    sp->do_write(data);
                                }
                                );
                            }
                        },
                            [wp](std::string&& reason) { // CloseFunc
                            if (auto sp = wp.lock())
                            {
                                boost::asio::post(
                                    sp->GetDerived().GetStream().get_executor(),
                                    [sp, reason = std::move(reason)]()
                                {
                                    websocket::close_reason cr;
                                    cr.reason = std::move(reason);
                                    sp->GetDerived().GetStream().async_close(cr, [sp](boost::system::error_code ec)
                                    {
                                        if (ec)
                                            return fail(ec, "close");
                                    });
                                }
                                );
                            }
                        });
                    }
                    sp->_handler->ReactorThread_onWSDataReceived(data);
                }
            });
        }

        void do_read()
        {
            // Read a message into our buffer
            GetDerived().GetStream().async_read(_buffer, 
                [sp = GetDerived().shared_from_this()](boost::system::error_code ec, std::size_t bytes)
            {
                sp->on_read(ec, bytes);
            });
        }

        void on_read(boost::system::error_code ec, std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);

            // This indicates that the Session was closed
            if (ec == websocket::error::closed)
                return;

            if (ec)
                return fail(ec, "read");

            {
                std::string data = boost::beast::buffers_to_string(_buffer.data());
                _buffer.consume(_buffer.size());

                if (!data.empty())
                {
                    process_data_async(std::move(data));
                }
            }

            do_read();
        }

        void do_write(const std::string& msg)
        {
            std::string* contents = nullptr;

            {
                _writeQueue.push(msg);

                if (_writeQueue.size() > 1)
                    return;

                contents = &_writeQueue.front();
            }

            GetDerived().GetStream().async_write(boost::asio::buffer(*contents),
                [sp = GetDerived().shared_from_this()](boost::system::error_code ec, std::size_t bytes)
            {
                sp->on_write(ec, bytes);
            });
        }

        void on_write(boost::system::error_code ec, std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);

            if (ec)
                return fail(ec, "write");

            std::string* contents = nullptr;
            {
                _writeQueue.pop();

                if (!_writeQueue.empty())
                {
                    contents = &_writeQueue.front();
                }
            }

            if (contents)
            {
                GetDerived().GetStream().async_write(
                    boost::asio::buffer(*contents),
                    [sp = GetDerived().shared_from_this()](boost::system::error_code ec, std::size_t bytes)
                {
                    sp->on_write(ec, bytes);
                });
            }
        }

        template<class Body, class Allocator>
        void do_accept(http::request<Body, http::basic_fields<Allocator>> req)
        {
            GetDerived().GetStream().async_accept(
                req,
                [sp = GetDerived().shared_from_this()](boost::system::error_code ec)
            {
                sp->on_accept(ec);
            });
        }

    private:
        boost::beast::multi_buffer _buffer;

        WebSocketServer::ClientHandler::Ptr _handler;
        SafeReactor::Ptr _reactor;
        HandlerCreator _creator;

        std::queue<std::string> _writeQueue;
    };

    class PlainWebsocketSession 
        : public WebsocketSession<PlainWebsocketSession>
        , public std::enable_shared_from_this<PlainWebsocketSession>
    {
    public:
        // Take ownership of the socket
        explicit PlainWebsocketSession(beast::tcp_stream&& tcpStream, boost::beast::multi_buffer&& buffer, SafeReactor::Ptr reactor, HandlerCreator creator)
            : WebsocketSession<PlainWebsocketSession>(std::move(buffer), reactor, creator)
            , _wstream(std::move(tcpStream))
        {

        }

        auto& GetStream()
        {
            return _wstream;
        }

    private:
        websocket::stream<beast::tcp_stream> _wstream;
    };

    class SecureWebsocketSession
        : public WebsocketSession<SecureWebsocketSession>
        , public std::enable_shared_from_this<SecureWebsocketSession>
    {
    public:
        // Take ownership of the socket
        explicit SecureWebsocketSession(beast::tcp_stream&& tcpStream, boost::beast::multi_buffer&& buffer, ssl::context& tlsContext, SafeReactor::Ptr reactor, HandlerCreator creator)
            : WebsocketSession<SecureWebsocketSession>(std::move(buffer), reactor, std::move(creator))
            , _wstream(std::move(tcpStream), tlsContext)
        {

        }

        void run();
        void on_handshake(beast::error_code ec, std::size_t bytesTransferred);

        auto& GetStream()
        {
            return _wstream;
        }

    private:
        websocket::stream<beast::ssl_stream<beast::tcp_stream>> _wstream;
    };

    // Handles an HTTP server connection, we use it to filter origin
    class HttpSession : public std::enable_shared_from_this<HttpSession>
    {
        // This queue is used for HTTP pipeline
        class Queue
        {
            // Maximum number of responses we will queue
            const size_t Limit = 256;

            // The type-erased, saved work item
            struct Work
            {
                virtual ~Work() = default;
                virtual void operator()() = 0;
            };

            HttpSession& _session;
            std::vector<std::unique_ptr<Work>> _items;

        public:
            explicit Queue(HttpSession& session)
                : _session(session)
            {
                assert(Limit > 0 && "queue limit must be positive");
                _items.reserve(Limit);
            }

            // Returns `true` if we have reached the queue limit
            [[nodiscard]] bool is_full() const
            {
                return _items.size() >= Limit;
            }

            // Called when a message finishes sending
            // Returns `true` if the caller should initiate a read
            [[nodiscard]] bool on_write()
            {
                assert(!_items.empty());

                auto const was_full = is_full();
                _items.erase(_items.begin());

                if (!_items.empty()){
                    (*_items.front())();
                }

                return was_full;
            }

            // Called by the HTTP handler to send a response.
            template<bool isRequest, class Body, class Fields>
            void operator()(http::message<isRequest, Body, Fields>&& msg)
            {
                // This holds a work item
                struct WorkImpl: Work
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

                    void operator()() final
                    {
                        http::async_write(
                            m_self._socket,
                            m_msg,
                            [sp = m_self.shared_from_this(), close = m_msg.need_eof()](beast::error_code ec, std::size_t bytes_transferred)
                        {
                            sp->on_write(close, ec, bytes_transferred);
                        });
                    }
                };

                // Allocate and store the work
                _items.push_back(boost::make_unique<WorkImpl>(_session, std::move(msg)));

                // If there was no previous work, start this one
                if (_items.size() == 1) {
                    (*_items.front())();
                }
            }
        };

    public:
        // Take ownership of the socket
        HttpSession(tcp::socket&& socket, SafeReactor::Ptr reactor, HandlerCreator creator, std::string allowedOrigin);
        void run();

    private:
        void do_read();
        void on_read(boost::beast::error_code ec, std::size_t bytes_transferred);
        void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred);
        void do_close();

        tcp::socket        _socket;
        Queue              _queue;
        beast::flat_buffer _buffer;
        SafeReactor::Ptr   _reactor;
        HandlerCreator     _handlerCreator;
        std::string        _allowedOrigin;
        http::request<http::string_body> _request;
    };

    //
    // Detects SSL handshakes
    //
    class DetectSession
        : public std::enable_shared_from_this<DetectSession>
    {
    public:
        explicit
            DetectSession(tcp::socket&& socket, ssl::context& ctx, SafeReactor::Ptr reactor, HandlerCreator creator)
            : _stream(std::move(socket))
            , _ctx(ctx)
            , _reactor(reactor)
            , _creator(creator)
        {
        }

        // Launch the detector
        void run()
        {
            // Set the timeout.
            _stream.expires_after(std::chrono::seconds(30));

            beast::async_detect_ssl(
                _stream,
                _buffer,
                [sp = shared_from_this()](beast::error_code ec, bool result)
            {
                sp->on_detect(ec, result);
            });
        }

        void on_detect(beast::error_code ec, bool result)
        {
            if (ec)
                return fail(ec, "detect");

            if (result)
            {
                // Launch SSL session
                std::make_shared<SecureWebsocketSession>(
                    std::move(_stream),
                    std::move(_buffer),
                    _ctx,
                    _reactor,
                    _creator)->run();
                return;
            }

            // Launch plain session
            std::make_shared<PlainWebsocketSession>(
                std::move(_stream),
                std::move(_buffer),
                _reactor,
                _creator)->run();
        }
    private:
        beast::tcp_stream _stream;
        ssl::context& _ctx;
        boost::beast::multi_buffer _buffer;
        SafeReactor::Ptr _reactor;
        HandlerCreator _creator;
    };
}