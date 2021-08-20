#include "wallet/unittests/test_helpers.h"
#include "utility/logger.h"
#include "websocket/websocket_server.h"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <iostream>

WALLET_TEST_INIT

using namespace beam;
namespace
{

    namespace beast = boost::beast;         // from <boost/beast.hpp>
    namespace http = beast::http;           // from <boost/beast/http.hpp>
    namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
    namespace net = boost::asio;            // from <boost/asio.hpp>
    using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

    int SendMessage(std::string host, const std::string& port, const std::string& text)
    {
        try
        {
            // The io_context is required for all I/O
            net::io_context ioc;

            // These objects perform our I/O
            tcp::resolver resolver{ ioc };
            websocket::stream<tcp::socket> ws{ ioc };

            // Look up the domain name
            auto const results = resolver.resolve(host, port);

            // Make the connection on the IP address we get from a lookup
            auto ep = net::connect(ws.next_layer(), results);

            // Update the host_ string. This will provide the value of the
            // Host HTTP header during the WebSocket handshake.
            // See https://tools.ietf.org/html/rfc7230#section-5.4
            host += ':' + std::to_string(ep.port());

            // Set a decorator to change the User-Agent of the handshake
            ws.set_option(websocket::stream_base::decorator(
                [](websocket::request_type& req)
            {
                req.set(http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                    " websocket-client-coro");
            }));

            // Perform the websocket handshake
            ws.handshake(host, "/");

            // Send the message
            ws.write(net::buffer(std::string(text)));

            // This buffer will hold the incoming message
            beast::flat_buffer buffer;

            // Read a message into our buffer
            ws.read(buffer);

            // Close the WebSocket connection
            ws.close(websocket::close_code::normal);

            std::stringstream ss;
            ss << beast::make_printable(buffer.data());
            WALLET_CHECK(ss.str() == "test messagetest message");

            // If we get here then the connection is closed gracefully

            // The make_printable() function helps print a ConstBufferSequence
            //std::cout << beast::make_printable(buffer.data()) << std::endl;
        }
        catch (std::exception const& e)
        {
            WALLET_CHECK(false);
            std::cerr << "Error: " << e.what() << std::endl;
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    void RunClient(std::string host, const std::string& port, const std::string& text)
    {
        std::thread(SendMessage, host, port, text).detach();
    }

    template<typename H>
    class MyWebSocketServer : public WebSocketServer
    {
    public:
        MyWebSocketServer(SafeReactor::Ptr reactor, uint16_t port)
            : WebSocketServer(std::move(reactor), port, "")
        {
        }

        virtual ~MyWebSocketServer() = default;

    private:
        WebSocketServer::ClientHandler::Ptr ReactorThread_onNewWSClient(WebSocketServer::SendFunc wsSend, WebSocketServer::CloseFunc wsClose) override
        {
            return std::make_shared<H>(wsSend, wsClose);
        }
    };


    void PlainWebsocketTest()
    {
        std::cout << "Plain Web Socket test" << std::endl;

        try
        {
            struct MyClientHandler : WebSocketServer::ClientHandler
            {
                WebSocketServer::SendFunc m_wsSend;
                WebSocketServer::CloseFunc m_wsClose;
                MyClientHandler(WebSocketServer::SendFunc wsSend, WebSocketServer::CloseFunc wsClose)
                    : m_wsSend(wsSend)
                {}
                virtual ~MyClientHandler()
                {
                    io::Reactor::get_Current().stop();
                }
                void ReactorThread_onWSDataReceived(const std::string& message) override
                {
                    WALLET_CHECK(message == "test message");
                    m_wsSend(message + message);
                    
                }
            };
            SafeReactor::Ptr safeReactor = SafeReactor::create();
            io::Reactor::Ptr reactor = safeReactor->ptr();
            io::Reactor::Scope scope(*reactor);
            MyWebSocketServer<MyClientHandler> server(safeReactor, 8200);
            RunClient("127.0.0.1", "8200", "test message");
            reactor->run();
        }
        catch (...)
        {
            WALLET_CHECK(false);
        }
    }
}


int main()
{
    int logLevel = LOG_LEVEL_WARNING;
    auto logger = beam::Logger::create(logLevel, logLevel);

    PlainWebsocketTest();


    return WALLET_CHECK_RESULT;
}