#include "wallet/unittests/test_helpers.h"
#include "utility/logger.h"
#include "websocket/websocket_server.h"

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
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
    namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
    using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

    void LoadRootCertificates(ssl::context& ctx, boost::system::error_code& ec)
    {
        std::string const cert =

            "-----BEGIN CERTIFICATE-----\n"
            "MIIDxTCCAq2gAwIBAgIQAqxcJmoLQJuPC3nyrkYldzANBgkqhkiG9w0BAQUFADBs\n"
            "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
            "d3cuZGlnaWNlcnQuY29tMSswKQYDVQQDEyJEaWdpQ2VydCBIaWdoIEFzc3VyYW5j\n"
            "ZSBFViBSb290IENBMB4XDTA2MTExMDAwMDAwMFoXDTMxMTExMDAwMDAwMFowbDEL\n"
            "MAkGA1UEBhMCVVMxFTATBgNVBAoTDERpZ2lDZXJ0IEluYzEZMBcGA1UECxMQd3d3\n"
            "LmRpZ2ljZXJ0LmNvbTErMCkGA1UEAxMiRGlnaUNlcnQgSGlnaCBBc3N1cmFuY2Ug\n"
            "RVYgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMbM5XPm\n"
            "+9S75S0tMqbf5YE/yc0lSbZxKsPVlDRnogocsF9ppkCxxLeyj9CYpKlBWTrT3JTW\n"
            "PNt0OKRKzE0lgvdKpVMSOO7zSW1xkX5jtqumX8OkhPhPYlG++MXs2ziS4wblCJEM\n"
            "xChBVfvLWokVfnHoNb9Ncgk9vjo4UFt3MRuNs8ckRZqnrG0AFFoEt7oT61EKmEFB\n"
            "Ik5lYYeBQVCmeVyJ3hlKV9Uu5l0cUyx+mM0aBhakaHPQNAQTXKFx01p8VdteZOE3\n"
            "hzBWBOURtCmAEvF5OYiiAhF8J2a3iLd48soKqDirCmTCv2ZdlYTBoSUeh10aUAsg\n"
            "EsxBu24LUTi4S8sCAwEAAaNjMGEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB/wQF\n"
            "MAMBAf8wHQYDVR0OBBYEFLE+w2kD+L9HAdSYJhoIAu9jZCvDMB8GA1UdIwQYMBaA\n"
            "FLE+w2kD+L9HAdSYJhoIAu9jZCvDMA0GCSqGSIb3DQEBBQUAA4IBAQAcGgaX3Nec\n"
            "nzyIZgYIVyHbIUf4KmeqvxgydkAQV8GK83rZEWWONfqe/EW1ntlMMUu4kehDLI6z\n"
            "eM7b41N5cdblIZQB2lWHmiRk9opmzN6cN82oNLFpmyPInngiK3BD41VHMWEZ71jF\n"
            "hS9OMPagMRYjyOfiZRYzy78aG6A9+MpeizGLYAiJLQwGXFK3xPkKmNEVX58Svnw2\n"
            "Yzi9RKR/5CYrCsSXaQ3pjOLAEFe4yHYSkVXySGnYvCoCWw9E1CAx2/S6cCZdkGCe\n"
            "vEsXCS+0yx5DaMkHJ8HSXPfqIbloEpw8nL+e/IBcm2PN7EeqJSdnoDfzAIJ9VNep\n"
            "+OkuE6N36B9K\n"
            "-----END CERTIFICATE-----\n"

            "-----BEGIN CERTIFICATE-----\n"
            "MIIDaDCCAlCgAwIBAgIJAO8vBu8i8exWMA0GCSqGSIb3DQEBCwUAMEkxCzAJBgNV\n"
            "BAYTAlVTMQswCQYDVQQIDAJDQTEtMCsGA1UEBwwkTG9zIEFuZ2VsZXNPPUJlYXN0\n"
            "Q049d3d3LmV4YW1wbGUuY29tMB4XDTE3MDUwMzE4MzkxMloXDTQ0MDkxODE4Mzkx\n"
            "MlowSTELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAkNBMS0wKwYDVQQHDCRMb3MgQW5n\n"
            "ZWxlc089QmVhc3RDTj13d3cuZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEBAQUA\n"
            "A4IBDwAwggEKAoIBAQDJ7BRKFO8fqmsEXw8v9YOVXyrQVsVbjSSGEs4Vzs4cJgcF\n"
            "xqGitbnLIrOgiJpRAPLy5MNcAXE1strVGfdEf7xMYSZ/4wOrxUyVw/Ltgsft8m7b\n"
            "Fu8TsCzO6XrxpnVtWk506YZ7ToTa5UjHfBi2+pWTxbpN12UhiZNUcrRsqTFW+6fO\n"
            "9d7xm5wlaZG8cMdg0cO1bhkz45JSl3wWKIES7t3EfKePZbNlQ5hPy7Pd5JTmdGBp\n"
            "yY8anC8u4LPbmgW0/U31PH0rRVfGcBbZsAoQw5Tc5dnb6N2GEIbq3ehSfdDHGnrv\n"
            "enu2tOK9Qx6GEzXh3sekZkxcgh+NlIxCNxu//Dk9AgMBAAGjUzBRMB0GA1UdDgQW\n"
            "BBTZh0N9Ne1OD7GBGJYz4PNESHuXezAfBgNVHSMEGDAWgBTZh0N9Ne1OD7GBGJYz\n"
            "4PNESHuXezAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQCmTJVT\n"
            "LH5Cru1vXtzb3N9dyolcVH82xFVwPewArchgq+CEkajOU9bnzCqvhM4CryBb4cUs\n"
            "gqXWp85hAh55uBOqXb2yyESEleMCJEiVTwm/m26FdONvEGptsiCmF5Gxi0YRtn8N\n"
            "V+KhrQaAyLrLdPYI7TrwAOisq2I1cD0mt+xgwuv/654Rl3IhOMx+fKWKJ9qLAiaE\n"
            "fQyshjlPP9mYVxWOxqctUdQ8UnsUKKGEUcVrA08i1OAnVKlPFjKBvk+r7jpsTPcr\n"
            "9pWXTO9JrYMML7d+XRSZA1n3856OqZDX4403+9FnXCvfcLZLLKTBvwwFgEFGpzjK\n"
            "UEVbkhd5qstF6qWK\n"
            "-----END CERTIFICATE-----\n";
        ;

        ctx.add_certificate_authority(
            boost::asio::buffer(cert.data(), cert.size()), ec);
        if (ec)
            return;
    }

    void LoadServerCertificate(boost::asio::ssl::context& ctx)
    {
        /*
            The certificate was generated from CMD.EXE on Windows 10 using:

            winpty openssl dhparam -out dh.pem 2048
            winpty openssl req -newkey rsa:2048 -nodes -keyout key.pem -x509 -days 10000 -out cert.pem -subj "//C=US\ST=CA\L=Los Angeles\O=Beast\CN=www.example.com"
        */

        std::string const cert =
            "-----BEGIN CERTIFICATE-----\n"
            "MIIDaDCCAlCgAwIBAgIJAO8vBu8i8exWMA0GCSqGSIb3DQEBCwUAMEkxCzAJBgNV\n"
            "BAYTAlVTMQswCQYDVQQIDAJDQTEtMCsGA1UEBwwkTG9zIEFuZ2VsZXNPPUJlYXN0\n"
            "Q049d3d3LmV4YW1wbGUuY29tMB4XDTE3MDUwMzE4MzkxMloXDTQ0MDkxODE4Mzkx\n"
            "MlowSTELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAkNBMS0wKwYDVQQHDCRMb3MgQW5n\n"
            "ZWxlc089QmVhc3RDTj13d3cuZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEBAQUA\n"
            "A4IBDwAwggEKAoIBAQDJ7BRKFO8fqmsEXw8v9YOVXyrQVsVbjSSGEs4Vzs4cJgcF\n"
            "xqGitbnLIrOgiJpRAPLy5MNcAXE1strVGfdEf7xMYSZ/4wOrxUyVw/Ltgsft8m7b\n"
            "Fu8TsCzO6XrxpnVtWk506YZ7ToTa5UjHfBi2+pWTxbpN12UhiZNUcrRsqTFW+6fO\n"
            "9d7xm5wlaZG8cMdg0cO1bhkz45JSl3wWKIES7t3EfKePZbNlQ5hPy7Pd5JTmdGBp\n"
            "yY8anC8u4LPbmgW0/U31PH0rRVfGcBbZsAoQw5Tc5dnb6N2GEIbq3ehSfdDHGnrv\n"
            "enu2tOK9Qx6GEzXh3sekZkxcgh+NlIxCNxu//Dk9AgMBAAGjUzBRMB0GA1UdDgQW\n"
            "BBTZh0N9Ne1OD7GBGJYz4PNESHuXezAfBgNVHSMEGDAWgBTZh0N9Ne1OD7GBGJYz\n"
            "4PNESHuXezAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQCmTJVT\n"
            "LH5Cru1vXtzb3N9dyolcVH82xFVwPewArchgq+CEkajOU9bnzCqvhM4CryBb4cUs\n"
            "gqXWp85hAh55uBOqXb2yyESEleMCJEiVTwm/m26FdONvEGptsiCmF5Gxi0YRtn8N\n"
            "V+KhrQaAyLrLdPYI7TrwAOisq2I1cD0mt+xgwuv/654Rl3IhOMx+fKWKJ9qLAiaE\n"
            "fQyshjlPP9mYVxWOxqctUdQ8UnsUKKGEUcVrA08i1OAnVKlPFjKBvk+r7jpsTPcr\n"
            "9pWXTO9JrYMML7d+XRSZA1n3856OqZDX4403+9FnXCvfcLZLLKTBvwwFgEFGpzjK\n"
            "UEVbkhd5qstF6qWK\n"
            "-----END CERTIFICATE-----\n";

        std::string const key =
            "-----BEGIN PRIVATE KEY-----\n"
            "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDJ7BRKFO8fqmsE\n"
            "Xw8v9YOVXyrQVsVbjSSGEs4Vzs4cJgcFxqGitbnLIrOgiJpRAPLy5MNcAXE1strV\n"
            "GfdEf7xMYSZ/4wOrxUyVw/Ltgsft8m7bFu8TsCzO6XrxpnVtWk506YZ7ToTa5UjH\n"
            "fBi2+pWTxbpN12UhiZNUcrRsqTFW+6fO9d7xm5wlaZG8cMdg0cO1bhkz45JSl3wW\n"
            "KIES7t3EfKePZbNlQ5hPy7Pd5JTmdGBpyY8anC8u4LPbmgW0/U31PH0rRVfGcBbZ\n"
            "sAoQw5Tc5dnb6N2GEIbq3ehSfdDHGnrvenu2tOK9Qx6GEzXh3sekZkxcgh+NlIxC\n"
            "Nxu//Dk9AgMBAAECggEBAK1gV8uETg4SdfE67f9v/5uyK0DYQH1ro4C7hNiUycTB\n"
            "oiYDd6YOA4m4MiQVJuuGtRR5+IR3eI1zFRMFSJs4UqYChNwqQGys7CVsKpplQOW+\n"
            "1BCqkH2HN/Ix5662Dv3mHJemLCKUON77IJKoq0/xuZ04mc9csykox6grFWB3pjXY\n"
            "OEn9U8pt5KNldWfpfAZ7xu9WfyvthGXlhfwKEetOuHfAQv7FF6s25UIEU6Hmnwp9\n"
            "VmYp2twfMGdztz/gfFjKOGxf92RG+FMSkyAPq/vhyB7oQWxa+vdBn6BSdsfn27Qs\n"
            "bTvXrGe4FYcbuw4WkAKTljZX7TUegkXiwFoSps0jegECgYEA7o5AcRTZVUmmSs8W\n"
            "PUHn89UEuDAMFVk7grG1bg8exLQSpugCykcqXt1WNrqB7x6nB+dbVANWNhSmhgCg\n"
            "VrV941vbx8ketqZ9YInSbGPWIU/tss3r8Yx2Ct3mQpvpGC6iGHzEc/NHJP8Efvh/\n"
            "CcUWmLjLGJYYeP5oNu5cncC3fXUCgYEA2LANATm0A6sFVGe3sSLO9un1brA4zlZE\n"
            "Hjd3KOZnMPt73B426qUOcw5B2wIS8GJsUES0P94pKg83oyzmoUV9vJpJLjHA4qmL\n"
            "CDAd6CjAmE5ea4dFdZwDDS8F9FntJMdPQJA9vq+JaeS+k7ds3+7oiNe+RUIHR1Sz\n"
            "VEAKh3Xw66kCgYB7KO/2Mchesu5qku2tZJhHF4QfP5cNcos511uO3bmJ3ln+16uR\n"
            "GRqz7Vu0V6f7dvzPJM/O2QYqV5D9f9dHzN2YgvU9+QSlUeFK9PyxPv3vJt/WP1//\n"
            "zf+nbpaRbwLxnCnNsKSQJFpnrE166/pSZfFbmZQpNlyeIuJU8czZGQTifQKBgHXe\n"
            "/pQGEZhVNab+bHwdFTxXdDzr+1qyrodJYLaM7uFES9InVXQ6qSuJO+WosSi2QXlA\n"
            "hlSfwwCwGnHXAPYFWSp5Owm34tbpp0mi8wHQ+UNgjhgsE2qwnTBUvgZ3zHpPORtD\n"
            "23KZBkTmO40bIEyIJ1IZGdWO32q79nkEBTY+v/lRAoGBAI1rbouFYPBrTYQ9kcjt\n"
            "1yfu4JF5MvO9JrHQ9tOwkqDmNCWx9xWXbgydsn/eFtuUMULWsG3lNjfst/Esb8ch\n"
            "k5cZd6pdJZa4/vhEwrYYSuEjMCnRb0lUsm7TsHxQrUd6Fi/mUuFU/haC0o0chLq7\n"
            "pVOUFq5mW8p0zbtfHbjkgxyF\n"
            "-----END PRIVATE KEY-----\n";

        std::string const dh =
            "-----BEGIN DH PARAMETERS-----\n"
            "MIIBCAKCAQEArzQc5mpm0Fs8yahDeySj31JZlwEphUdZ9StM2D8+Fo7TMduGtSi+\n"
            "/HRWVwHcTFAgrxVdm+dl474mOUqqaz4MpzIb6+6OVfWHbQJmXPepZKyu4LgUPvY/\n"
            "4q3/iDMjIS0fLOu/bLuObwU5ccZmDgfhmz1GanRlTQOiYRty3FiOATWZBRh6uv4u\n"
            "tff4A9Bm3V9tLx9S6djq31w31Gl7OQhryodW28kc16t9TvO1BzcV3HjRPwpe701X\n"
            "oEEZdnZWANkkpR/m/pfgdmGPU66S2sXMHgsliViQWpDCYeehrvFRHEdR9NV+XJfC\n"
            "QMUk26jPTIVTLfXmmwU0u8vUkpR7LQKkwwIBAg==\n"
            "-----END DH PARAMETERS-----\n";

        ctx.set_password_callback(
            [](std::size_t,
                boost::asio::ssl::context_base::password_purpose)
        {
            return "test";
        });

        ctx.set_options(
            boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::single_dh_use);

        ctx.use_certificate_chain(
            boost::asio::buffer(cert.data(), cert.size()));

        ctx.use_private_key(
            boost::asio::buffer(key.data(), key.size()),
            boost::asio::ssl::context::file_format::pem);

        ctx.use_tmp_dh(
            boost::asio::buffer(dh.data(), dh.size()));
    }


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

    int SendSecureMessage(std::string host, const std::string& port, const std::string& text)
    {
        try
        {
            // The io_context is required for all I/O
            net::io_context ioc;

            // The SSL context is required, and holds certificates
            ssl::context ctx{ ssl::context::tlsv12_client };

            // This holds the root certificate used for verification
            boost::system::error_code ec;
            LoadRootCertificates(ctx, ec);

            // These objects perform our I/O
            tcp::resolver resolver{ ioc };
            websocket::stream<beast::ssl_stream<tcp::socket>> ws{ ioc, ctx };

            // Look up the domain name
            auto const results = resolver.resolve(host, port);

            // Make the connection on the IP address we get from a lookup
            net::connect(ws.next_layer().next_layer(), results.begin(), results.end());

            // Perform the SSL handshake
            ws.next_layer().handshake(ssl::stream_base::client);


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

    template<typename H>
    class MyWebSocketServer : public WebSocketServer
    {
    public:
        MyWebSocketServer(SafeReactor::Ptr reactor, uint16_t port, ssl::context* tlsContext = nullptr)
            : WebSocketServer(std::move(reactor), port, "", tlsContext)
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
            std::thread(SendMessage, "127.0.0.1", "8200", "test message").detach();
            reactor->run();
        }
        catch (...)
        {
            WALLET_CHECK(false);
        }
    }

    void SecureWebsocketTest()
    {
        std::cout << "Secure Web Socket test" << std::endl;

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
            ssl::context tlsContext{ ssl::context::tlsv12 };
            LoadServerCertificate(tlsContext);
            MyWebSocketServer<MyClientHandler> server(safeReactor, 8200, &tlsContext);
            std::thread(SendSecureMessage, "127.0.0.1", "8200", "test message").detach();
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
    SecureWebsocketTest();


    return WALLET_CHECK_RESULT;
}