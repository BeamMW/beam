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
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include "websocket_server.h"
#include "utility/io/reactor.h"
#include "utility/io/asyncevent.h"

namespace beam::wallet {
    namespace beast     = boost::beast;
    namespace http      = boost::beast::http;
    namespace websocket = boost::beast::websocket;

    void fail(boost::system::error_code ec, char const* what);
    void failEx(boost::system::error_code ec, char const* what);

    using tcp = boost::asio::ip::tcp;
    using HandlerCreator = std::function<WebSocketServer::ClientHandler::Ptr (WebSocketServer::SendFunc, WebSocketServer::CloseFunc)>;

    class WebsocketSession
            : public std::enable_shared_from_this<WebsocketSession>
    {
    public:
        // Take ownership of the socket
        explicit WebsocketSession(tcp::socket socket, SafeReactor::Ptr reactor, HandlerCreator creator);
        ~WebsocketSession();

        void run();
        void on_accept(boost::system::error_code ec);
        void do_read();
        void on_read(boost::system::error_code ec, std::size_t bytes_transferred);
        void process_data_async(std::string&& data);
        void do_write(const std::string& msg);
        void on_write(boost::system::error_code ec, std::size_t bytes_transferred);

        template<class Body, class Allocator>
        void do_accept(http::request<Body, http::basic_fields<Allocator>> req)
        {
            _wsocket.async_accept(
                req,
                [sp = shared_from_this()](boost::system::error_code ec)
            {
                sp->on_accept(ec);
            });
        }

    private:
        websocket::stream<tcp::socket> _wsocket;
        boost::beast::multi_buffer _buffer;

        WebSocketServer::ClientHandler::Ptr _handler;
        SafeReactor::Ptr _reactor;
        HandlerCreator _creator;

        std::queue<std::string> _writeQueue;
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
}