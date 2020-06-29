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
#include "sessions.h"
#include "utility/logger.h"
//#include <boost/beast/core/make_printable.hpp>

namespace beam::wallet {
    void fail(boost::system::error_code ec, char const* what)
    {
        LOG_ERROR() << what << ": " << ec.message();
    }

    void failEx(boost::system::error_code ec, char const* what)
    {
        fail(ec, what);
        throw std::runtime_error(ec.message());
    }

    //
    //
    // WebSocket Session
    //
    //
    WebsocketSession::WebsocketSession(tcp::socket socket, io::Reactor::Ptr reactor, const HandlerCreator& creator)
        : _wsocket(std::move(socket))
    {
        _handler = creator([this] (const std::string& data) {
            do_write(data);
        });

        _newDataEvent = io::AsyncEvent::create(*reactor, [this]() {
            while (true)
            {
                std::string data;
                {
                    std::unique_lock<std::mutex> lock(_queueMutex);
                    if (_dataQueue.empty())
                        return;
                    data = _dataQueue.front();
                    _dataQueue.pop();
                }
                _handler->onWSDataReceived(data);
            }
        });
    }

    // Start the asynchronous operation
    void WebsocketSession::run()
    {
        // Accept the websocket handshake
        _wsocket.async_accept([sp = shared_from_this()](boost::system::error_code ec) {
            sp->on_accept(ec);
        });
    }

    void WebsocketSession::on_accept(boost::system::error_code ec)
    {
        if (ec)
            return fail(ec, "accept");

        // Read a message
        do_read();
    }

    void WebsocketSession::do_read()
    {
        // Read a message into our buffer
        _wsocket.async_read(_buffer, [sp = shared_from_this()](boost::system::error_code ec, std::size_t bytes) {
            sp->on_read(ec, bytes);
        });
    }

    void WebsocketSession::on_read(boost::system::error_code ec, std::size_t bytes_transferred)
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
            os << boost::beast::make_printable(_buffer.data());
# else
            os << boost::beast::buffers(_buffer.data());
# endif
            _buffer.consume(_buffer.size());
            auto data = os.str();

            if (!data.empty())
            {
                process_data_async(std::move(data));
            }
        }

        do_read();
    }

    void WebsocketSession::process_data_async(std::string&& data)
    {
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _dataQueue.push(std::move(data));
        }
        _newDataEvent->post();
    }

    void WebsocketSession::do_write(const std::string &msg)
    {
        std::string* contents = nullptr;

        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _writeQueue.push(msg);

            if (_writeQueue.size() > 1)
                return;

            contents = &_writeQueue.front();
        }

        _wsocket.async_write(boost::asio::buffer(*contents), [sp = shared_from_this()](boost::system::error_code ec, std::size_t bytes)
        {
            sp->on_write(ec, bytes);
        });
    }

    void WebsocketSession::on_write(boost::system::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "write");

        std::string* contents = nullptr;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _writeQueue.pop();

            if (!_writeQueue.empty())
            {
                contents = &_writeQueue.front();
            }
        }

        if (contents)
        {
            _wsocket.async_write(
                boost::asio::buffer(*contents),
                [sp = shared_from_this()](boost::system::error_code ec, std::size_t bytes)
            {
                sp->on_write(ec, bytes);
            });
        }
    }


    //
    //
    //  HTTP Session
    //
    //
    HttpSession::HttpSession(boost::asio::ip::tcp::socket&& socket, io::Reactor::Ptr reactor, HandlerCreator creator, std::string allowedOrigin)
        : _socket(std::move(socket))
        , _queue(*this)
        , _reactor(std::move(reactor))
        , _handlerCreator(std::move(creator))
        , _allowedOrigin(std::move(allowedOrigin))
    {
    }

    // Start the session
    void HttpSession::run()
    {
        do_read();
    }

    // Read request using the parser-oriented interface
    void HttpSession::do_read()
    {
        http::request<http::string_body> request;
        http::async_read(
            _socket,
            _buffer,
            _request,
            [sp = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred)
        {
            sp->on_read(ec, bytes_transferred);
        });
    }

    void HttpSession::on_read(boost::beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        // This means they closed the connection
        if (ec == http::error::end_of_stream)
            return do_close();

        if (ec)
            return fail(ec, "read");

        {
            auto const forbiddenResponse = [this](beast::string_view why)
            {
                http::response<http::string_body> res;
                res.version(_request.version());
                res.result(http::status::forbidden);
                res.body() = std::string(why);
                res.prepare_payload();
                return res;
            };

            if (!beast::iequals(_request[http::field::origin], _allowedOrigin))
            {
                _queue(forbiddenResponse("origin is not allowed"));
                return;
            }
        }

        // Create a websocket session, transferring ownership
        // of both the socket and the HTTP request.
        std::make_shared<WebsocketSession>(std::move(_socket), _reactor, _handlerCreator)->do_accept(std::move(_request));
    }

    void HttpSession::on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
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
        if (_queue.on_write())
        {
            // Read another request
            do_read();
        }
    }

    void HttpSession::do_close()
    {
        // Send a TCP shutdown
        beast::error_code ec;
        _socket.shutdown(tcp::socket::shutdown_send, ec);
        // At this point the connection is closed gracefully
    }
}
