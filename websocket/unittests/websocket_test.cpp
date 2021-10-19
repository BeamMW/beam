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

    void LoadServerCertificate(WebSocketServer::Options& options)
    {
        /*
            The certificate was generated from CMD.EXE on Windows 10 using:

            winpty openssl dhparam -out dh.pem 2048
            winpty openssl req -newkey rsa:2048 -nodes -keyout key.pem -x509 -days 10000 -out cert.pem -subj "//C=US\ST=CA\L=Los Angeles\O=Beast\CN=www.example.com"
        */

        std::string const cert =
            "-----BEGIN CERTIFICATE-----\n"
            "MIIC4TCCAcmgAwIBAgIUZI8OTxIJc4I3qP93o3+7kkauMwkwDQYJKoZIhvcNAQEL\n"
            "BQAwADAeFw0yMTA4MzAxMjUyMzdaFw00OTAxMTUxMjUyMzdaMAAwggEiMA0GCSqG\n"
            "SIb3DQEBAQUAA4IBDwAwggEKAoIBAQCuuDs1tuf8otgGWO9fKW2RNeIfi9aH7u0r\n"
            "5VtFh8PLZiJjItzzNykxJWKGMlEyjS+R8czNSWylxgxvEQLyoUOKnDIO+zrS84T/\n"
            "XxRPPqukB/UuruuWMPmjumXPLp0MePSPAzY4IAMsS89ve0zoNh4R2Zzj33Y47L8R\n"
            "wWxW6Q5mRprT2o1UCJcledACWF4drqWDHOLNk2VOobdsfSLAKT+z2xq69wm3qBzQ\n"
            "2HZROsCsqQOUVG0pYlLevH65a/rlJ1kbYg4Tnf5ldcQdEJuaZFm6o9rn4xioQjRg\n"
            "4suG6/aFEyBTEgRlxRRmnSkqg7/UDVxKcaBkHsZoBEnJsWlExLC1AgMBAAGjUzBR\n"
            "MB0GA1UdDgQWBBRESesuAscp2fHe/v9xUMMbIipiqzAfBgNVHSMEGDAWgBRESesu\n"
            "Ascp2fHe/v9xUMMbIipiqzAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUA\n"
            "A4IBAQB4L2wS5w+MGEvZYsPk9f7ryN2c029ZJ5shmxOX4xYa9ig1ZloNox+v/05A\n"
            "CUVHnTaxAsFhZunbkXLQ5KAVzVuXQk2ljlWWhZjp5ImqQSTbkY2wzBNHIPbnSNEV\n"
            "CuprV51JDZrB1Q0zKulCq2ia1Og29FGclUHrL2QdMR30UZyV8HaCez0tEp4QSKNk\n"
            "tCd2tIZo8+n9UYCKAgp5FMhxpyL5DL94TCTTG1Lf4tEICeZdKV89a0d3eAN79kC7\n"
            "Tm2A/1SNQouuLdv+tQ/gMAfMCzqOMTqZ85oJOyqXnxAA9cvje5nm4pBn5Q/6H4ry\n"
            "oJXOdnELFSfHV5wdsQeq5IyO5A5X\n"
            "-----END CERTIFICATE-----\n";

        std::string const key =
            "-----BEGIN PRIVATE KEY-----\n"
            "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCuuDs1tuf8otgG\n"
            "WO9fKW2RNeIfi9aH7u0r5VtFh8PLZiJjItzzNykxJWKGMlEyjS+R8czNSWylxgxv\n"
            "EQLyoUOKnDIO+zrS84T/XxRPPqukB/UuruuWMPmjumXPLp0MePSPAzY4IAMsS89v\n"
            "e0zoNh4R2Zzj33Y47L8RwWxW6Q5mRprT2o1UCJcledACWF4drqWDHOLNk2VOobds\n"
            "fSLAKT+z2xq69wm3qBzQ2HZROsCsqQOUVG0pYlLevH65a/rlJ1kbYg4Tnf5ldcQd\n"
            "EJuaZFm6o9rn4xioQjRg4suG6/aFEyBTEgRlxRRmnSkqg7/UDVxKcaBkHsZoBEnJ\n"
            "sWlExLC1AgMBAAECggEAHnmF4lWX2ynwMhM7FUcdlFFosoXqkmUrOxfTNqp6jTSw\n"
            "VMhU75s0dR0HNU77eKzFmlgpl7jx2WxU6N53vChCpp+d350UYo0VKpHD8hqFR6QX\n"
            "sN/TuaurL1KoxV1lCTLjvTobL+jthMFoWhKQlIQz9HsXcWudrEom/YrWQcZ+u3nV\n"
            "KDGia5XVQxvD7+vHJ5ppZTM5wB5cUM6VNqwvkXO3Ts3zXJQ+fwbce1I4yg7xzm/m\n"
            "aBq1+z0BtkfxAa+W/D/rx89gPGVapi8r0vved+5ei9B7KMyHCKgwrlxpJyOJ87S5\n"
            "A+g8HzFatQPoleqUfGZluXxtoFQSK6B4FtNwPKy8AQKBgQDl99uLONrPSnat4b+v\n"
            "5i/VcvvA9Ot4ryOnIul6KaNbp3Gr/DyVchrHEkvu7+HVV75kYhObReG9AsMn7a/B\n"
            "1rxbria5SbgJm0r+HW4oJzkO5DO1L5gOdbQiYxw5Jff+vuD/fBIyXNKKNFrzO5Cj\n"
            "mtbmYDzZ9gP4s3wQE5XATy69AQKBgQDCf1oH+AM7r10BH69A1HNvnYqdamEZ0Bem\n"
            "VG32Q0YGZCDS0UfJ8ybo/Sw0VRplEE3WFkGrQg4lleJ3kYRDuFUtInNXNmPWvuJH\n"
            "pCJt/WsnIrF5j7hPWY/9G0pg8KuP4I/yvIAIVXYXesh06akhN9OsIuhQtCnueejS\n"
            "jWG1/aYPtQKBgA8rWVUGrBBOZiO0J3PP6EnZKtggj8PHMb/doq8HPhpWoj3pBooJ\n"
            "G9ET2ORq+GedQRbYDVkJtAlGvF7O4/ASXRxjEXTZcwVXNAwtHs4RQEdGME78U7ho\n"
            "dThrdzoh0gkAyFCx+3VNACpTp8gxnqncFd7ebEUoYDywgjeBQziLQJcBAoGAfB+J\n"
            "9UvxxEVFtVHjJhxvDuwbahpZnX+PmDaJdn+4UJvV1rR4fAkQ69+mNj+ZeKXPBrFt\n"
            "dz3QiWv9+xCCuDULJqK1uRKc5I8tGUtGLatslq0tVcbCeOFPYtfnv7XXxxoow2BI\n"
            "1Qi1NIbHJtV3ehuGmnQsjlRr7iUe0EAp+1rEf4ECgYEAlG0xduNOFoIDcwkvVPE9\n"
            "K131PQZG2gPdj++sxtn+HArwkLfCehWF0rzTgwkTGAetnWIozkIGUMZyARGCNkf/\n"
            "KLah5NgArdQ5hPu8hR8E5VoZ7NqMgHmYchsEm9aHX6YdKZKFMHVAjii0OxFNI44G\n"
            "CnK97hhaayZ2X4FP2y75Ve0=\n"
            "-----END PRIVATE KEY-----\n";

        std::string const dh =
            "-----BEGIN DH PARAMETERS-----\n"
            "MIIBCAKCAQEAnu2ndoqGLBD+d/Sb7YCHTRYX+Eka4ps/M5j9WRjXsfZT4zsOaKDp\n"
            "8S0b6lyf09908RPyNnA9DrQC4kvlwYJ5xVoPTGG31w6B89BfiiUsykWczzXYWJNO\n"
            "wdOm0L2gpNDJgSlYhThG5ajNFwPVxCIkkmV82ysRoQyJbWhCtlIwRfuPWBVtuF2y\n"
            "/NpxMCD77hZD+VoqXdu3TfaCaA/bsGBQQtk5cJ2Xdaz/ZLLiNIpw2wGSe5p4Q4Uu\n"
            "GkIcZYhMsDM8sqQ0FE7J0Exx15vwTeGGtkRqdDne7IMdNsAbOO/QCX8dzm+qraI5\n"
            "XzKu+eTfSns9AeJTbxNDRy2M0OF/GbeIUwIBAg==\n"
            "-----END DH PARAMETERS-----\n";

        options.certificate = cert;
        options.key = key;
        options.dhParams = dh;

    }


    int SendMessage(std::string host, const std::string& port, const std::string& text, std::function<void()> cb)
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

        }
        catch (std::exception const& e)
        {
            WALLET_CHECK(false);
            std::cerr << "Error: " << e.what() << std::endl;
        }
        catch (...)
        {
            WALLET_CHECK(false);
            // std::cerr << "Error: " << e. << std::endl;
        }
        cb();
        return WALLET_CHECK_RESULT;
    }

    int SendSecureMessage(std::string host, const std::string& port, const std::string& text, std::function<void()> cb)
    {
        try
        {
            // The io_context is required for all I/O
            net::io_context ioc;

            // The SSL context is required, and holds certificates
            ssl::context ctx{ ssl::context::tlsv13_client };

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
            

        }
        catch (std::exception const& e)
        {
            WALLET_CHECK(false);
            std::cerr << "Error: " << e.what() << std::endl;
        }
        catch (...)
        {
            WALLET_CHECK(false);
           // std::cerr << "Error: " << e. << std::endl;
        }
        cb();
        return WALLET_CHECK_RESULT;
    }

    template<typename H>
    class MyWebSocketServer : public WebSocketServer
    {
    public:
        MyWebSocketServer(SafeReactor::Ptr reactor, const Options& options)
            : WebSocketServer(std::move(reactor), options)
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
                void ReactorThread_onWSDataReceived(const std::string& message) override
                {
                    std::cout << "Message: " << message << std::endl;
                    WALLET_CHECK(message == "test message");
                    m_wsSend(message + message);
                    
                }
            };
            SafeReactor::Ptr safeReactor = SafeReactor::create();
            io::Reactor::Ptr reactor = safeReactor->ptr();
            io::Reactor::Scope scope(*reactor);
            WebSocketServer::Options options;
            options.port = 8200;
            MyWebSocketServer<MyClientHandler> server(safeReactor, options);
            std::thread t1(SendMessage, "127.0.0.1", "8200", "test message", [reactor]() {reactor->stop(); });
            reactor->run();
            t1.join();
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
                void ReactorThread_onWSDataReceived(const std::string& message) override
                {
                    std::cout << "Secure Message: " << message << std::endl;
                    WALLET_CHECK(message == "test message");
                    m_wsSend(message + message);
                }
            };

            SafeReactor::Ptr safeReactor = SafeReactor::create();
            io::Reactor::Ptr reactor = safeReactor->ptr();
            io::Reactor::Scope scope(*reactor);

            size_t count = 2;
            auto cb = [&count, reactor]()
            {
                if (--count == 0)
                {
                    reactor->stop();
                }
            };

            WebSocketServer::Options options;
            options.port = 8200;
            options.useTls = true;
            LoadServerCertificate(options);
            MyWebSocketServer<MyClientHandler> server(safeReactor, options);
            std::thread t1(SendSecureMessage, "127.0.0.1", "8200", "test message", cb);
            std::thread t2(SendMessage, "127.0.0.1", "8200", "test message", cb);
            reactor->run();
            t1.join();
            t2.join();
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