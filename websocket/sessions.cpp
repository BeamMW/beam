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

namespace beam
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

    //
    // WebSocket Secure Session
    //
    void SecureWebsocketSession::run()
    {
        auto& ws = GetStream();
        ws.binary(true);
        // Set the timeout.
        beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(30));

        // Perform the SSL handshake
        ws.next_layer().async_handshake(
            ssl::stream_base::server,
            GetBuffer().data(),
            [sp = shared_from_this()](boost::system::error_code ec, std::size_t bytesTransferred)
        {
            sp->on_handshake(ec, bytesTransferred);
        });
    }

    void SecureWebsocketSession::on_handshake(beast::error_code ec, std::size_t bytesTransferred)
    {
        if (ec)
            return fail(ec, "handshake");

        GetBuffer().consume(bytesTransferred);
        auto& ws = GetStream();
        // Turn off the timeout on the tcp_stream, because
        // the websocket stream has its own timeout system.
        beast::get_lowest_layer(ws).expires_never();

        // Set suggested timeout settings for the websocket
        ws.set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::server));

        // Accept the websocket handshake
        ws.async_accept([sp = shared_from_this()](boost::system::error_code ec)
        {
            sp->on_accept(ec);
        });
    }

    //
    //  HTTP Session
    //
    HttpSession::HttpSession(boost::asio::ip::tcp::socket&& socket, SafeReactor::Ptr reactor, HandlerCreator creator, std::string allowedOrigin)
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
        //std::make_shared<WebsocketSession>(std::move(_socket), std::move(_buffer), _reactor, _handlerCreator)->do_accept(std::move(_request));
    }

    void HttpSession::on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "write");

        if (close)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic
            return do_close();
        }

        // Inform the queue that the write operation is completed
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
