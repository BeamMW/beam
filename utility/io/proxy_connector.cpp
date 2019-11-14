// Copyright 2019 The Beam Team
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

#include "proxy_connector.h"
#include "utility/io/tcpstream.h"

#include "utility/config.h"
#include "utility/helpers.h"

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif
#include "utility/logger.h"

namespace beam {
namespace io {

ProxyConnector::ProxyConnector(Reactor& r) :
    _reactor(r),
    _connectRequestsPool(config().get_int("io.connect_pool_size", 16, 0, 512)) {};

// ProxyConnector::~ProxyConnector() {
//     if (!_connectRequests.empty())
//     {
//         LOG_ERROR() << "connect requests were not cancelled";
//     }
//     if (!_cancelledConnectRequests.empty())
//     {
//         LOG_ERROR() << "callbacks on cancelled requests were not called";
//     }
// }

/**
 * @return Lambda called on tcp connection to proxy server.
 * Used to hold context and identify ProxyConnectRequest.
 */
ProxyConnector::OnConnect ProxyConnector::create_connection(
        uint64_t tag,
        Address destination,
        const OnConnect& on_proxy_establish,
        int timeoutMsec,
        bool tlsConnect) {
    
    ProxyConnectRequest* request_ptr = _connectRequestsPool.alloc();
    request_ptr->tag = tag;
    request_ptr->destination = destination;
    request_ptr->on_connection_establish = OnConnect(on_proxy_establish);
    request_ptr->on_proxy_response = OnResponse();
    request_ptr->timeoutMsec = timeoutMsec;
    request_ptr->tlsConnect = tlsConnect;
    _connectRequests[tag] = request_ptr;

    // Lambda used to hold context
    return [this](uint64_t tag, std::unique_ptr<TcpStream>&& new_stream, ErrorCode errorCode) {
        on_tcp_connect(tag, std::move(new_stream), errorCode);
    };
}

void ProxyConnector::delete_connection(uint64_t tag) {
    // TODO: proxy refact to modern pointers
    // TODO: check if @stream destructor is called properly
    _connectRequestsPool.release(_connectRequests[tag]);
    _connectRequests.erase(tag);
    // TODO: call user callback with error code
}

void ProxyConnector::on_tcp_connect(uint64_t tag, std::unique_ptr<TcpStream>&& new_stream, ErrorCode errorCode) {
    if (_connectRequests.count(tag) == 0) {
        LOG_DEBUG() << "Proxy connection undefined. Tag: " << tag;
        return; // ~TcpStream() has to close stream
    }
    if (errorCode != EC_OK) {
        LOG_DEBUG() << "Proxy server connection error. Tag: " << tag << ". Error: " << errorCode;
        delete_connection(tag);
        return;
    }

    LOG_DEBUG() << "Connected to proxy server. Tag: " << tag;

    // ProxyConnectRequest owns tcp stream instance during negotiation with proxy server.
    _connectRequests[tag]->stream = std::move(new_stream);
    send_auth_methods(tag);
}

void ProxyConnector::send_auth_methods(uint64_t tag) {
    // set callback for proxy auth method response
    _connectRequests[tag]->on_proxy_response = &ProxyConnector::on_auth_method_resp;
    _connectRequests[tag]->stream->enable_read(
        /* Lambda used to hold pointer ProxyConnector instance and ProxyConnectRequest tag
         * in TcpStream cause enable_read() doesn't pass info about
         * stream it was executed on.
         */
        [this, tag](ErrorCode errorCode, void *data, size_t size) {
            return _connectRequests[tag]->on_proxy_response(*this, tag, errorCode, data, size);
        });

    // send authentication methods to proxy server
    static const uint8_t auth_request[3] = {
        0x05,   // The VER field is set to X'05' for this version of the protocol.
        0x01,   // The NMETHODS field contains the number of method identifier octets
                // that appear in the METHODS field.
        0x00    // METHODS field. The values currently defined for METHOD are:
                // X'00' NO AUTHENTICATION REQUIRED
                // X'01' GSSAPI
                // X'02' USERNAME / PASSWORD
                // X'03' to X'7F' IANA ASSIGNED
                // X'80' to X'FE' RESERVED FOR PRIVATE METHODS
                // X'FF' NO ACCEPTABLE METHODS
    };
    Result res = _connectRequests[tag]->stream->write(auth_request, sizeof auth_request);
    {
        std::string req = beam::to_hex(auth_request, sizeof auth_request);
        LOG_DEBUG() << "Write to proxy " << sizeof auth_request << " bytes: " << req;
    }
    if (!res)
    {
        LOG_DEBUG() << "Write to proxy error: " << error_str(res.error());
        delete_connection(tag);
    }
}

bool ProxyConnector::on_auth_method_resp(uint64_t tag, ErrorCode errorCode, void *data, size_t size) {
    if (errorCode != EC_OK) {
        LOG_DEBUG() << "Proxy auth method response error. Tag: " << tag << ". Error: " << errorCode;
        delete_connection(tag);
    }
    if (data && size) {
        std::string response = beam::to_hex(data, size);
        LOG_DEBUG() << "Received from proxy " << size << " bytes: " << response;

        uint8_t *resp = static_cast<uint8_t *>(data);
        if (resp[0] != 0x05 || resp[1] != 0x00) {
            LOG_ERROR() << "Proxy required unsupported auth method.";
            delete_connection(tag);
        }
        // No authentication required
        send_connect_request(tag);        
    }
    else {
        LOG_DEBUG() << "Proxy auth method response error. size: " << size;
        delete_connection(tag);
    }
    return true;
}

void ProxyConnector::send_connect_request(uint64_t tag) {
    if (_connectRequests.count(tag) == 0) {
        return;
    }
    // set callback for proxy connection request reply
    _connectRequests[tag]->on_proxy_response = &ProxyConnector::on_connect_resp;
    
    Address &dest_addr = _connectRequests[tag]->destination;
    uint32_t ip = dest_addr.ip();
    uint16_t port = dest_addr.port();
    uint8_t conn_request[10] = {
        0x05,               // VER    protocol version: X'05'
        0x01,               // CMD:	  CONNECT X'01'
                            // 		  BIND X'02'
                            // 		  UDP ASSOCIATE X'03'
        0x00,               // RSV    RESERVED
        0x01,               // ATYP   address type of following address
                            //		  IP V4 address: X'01'
                            //		  DOMAINNAME: X'03'
                            //		  IP V6 address: X'04'
        ip >> 24,           // DST.ADDR desired destination address
        (ip >> 16) & 0xFF,
        (ip >> 8) & 0xFF,
        ip & 0xFF,
        static_cast<uint8_t>((port >> 8) & 0xFF), // DST.PORT desired destination port in network octet order
        static_cast<uint8_t>(port & 0xFF)
    };

    // Send connection request to proxy server
    Result res = _connectRequests[tag]->stream->write(conn_request, sizeof conn_request);
    {
        std::string req = beam::to_hex(conn_request, sizeof conn_request);
        LOG_DEBUG() << "Write to proxy " << sizeof conn_request << " bytes: " << req;
    }
    if (!res)
    {
        LOG_ERROR() << "Write to proxy error: " << error_str(res.error());
        delete_connection(tag);
    }
}

bool ProxyConnector::on_connect_resp(uint64_t tag, ErrorCode errorCode, void *data, size_t size) {
    if (errorCode != EC_OK) {
        LOG_DEBUG() << "Proxy connection response. Tag: " << tag << ". Error: " << errorCode;
    }
    if (data && size) {
        std::string response = beam::to_hex(data, size);
        LOG_DEBUG() << "Received from proxy " << size << " bytes: " << response;

        uint8_t *resp = static_cast<uint8_t *>(data);
        if (resp[0] != 0x05 || resp[1] != 0x00) {
            LOG_DEBUG() << "Proxy destination connect error.";
            delete_connection(tag);
        }
        on_connection_established(tag);
    }
    else {
        LOG_DEBUG() << "Proxy connection response error. size: " << size;
        delete_connection(tag);
    }
    return true;
}

void ProxyConnector::on_connection_established(uint64_t tag)
{
    if (_connectRequests.count(tag) == 0) {
        return;
    }

    // TcpStream::Ptr stream;
    // TcpStream *streamPtr = 0;
    // move TcpStream to SslStream ctor _connectRequests[tag]->stream;
    // if (request->isTls)
    // {
    //     if (!create_ssl_context())
    //     {
    //         errorCode = EC_SSL_ERROR;
    //     }
    //     else
    //     {
    //         streamPtr = new SslStream(_sslContext);
    //     }
    // }
    // else
    // {
    //     streamPtr = new TcpStream();
    // }
    // if (streamPtr)
    // {
    //     stream.reset(_reactor.stream_connected(streamPtr, request->handle));
    // }

    // if (_connectTimer)
    //     _connectTimer->cancel(request->tag);

    ProxyConnectRequest* request = _connectRequests[tag];
    request->stream->disable_read();
    request->on_proxy_response.~OnResponse();
    request->on_connection_establish(tag, std::move(_connectRequests[tag]->stream), EC_OK);
    request->on_connection_establish.~OnConnect();
    delete_connection(tag);
}

}   // namespace beam
}   // namespace io
