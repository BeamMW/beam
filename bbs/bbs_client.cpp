#include "bbs_client.h"
#include "utility/asynccontext.h"
#include "p2p/protocol.h"
#include "p2p/connection.h"
#include "p2p/types.h"

namespace beam { namespace bbs {

class ClientImpl : public Client, public IErrorHandler {
public:
    ClientImpl() :
        _protocol(0, 0, 1, 10, *this, 0x2000)
    {
        _protocol.add_message_handler<ClientImpl, Config, &ClientImpl::on_config_msg>(CONFIG_MSG_TYPE, this, 2, 20);
        _protocol.add_message_handler<ClientImpl, Servers, &ClientImpl::on_servers_msg>(SERVERS_MSG_TYPE, this, 0, 2000000);
        _protocol.add_message_handler<ClientImpl, Message, &ClientImpl::on_message>(MESSAGE_MSG_TYPE, this, 0, 2000000);

    }

    virtual ~ClientImpl() {
        // cleanup
    }

private:

    void on_protocol_error(uint64_t from, ProtocolError error) override {
        //StreamId streamId(from);
        //LOG_WARNING() << "protocol error " << error << " from " << streamId.address();
        //cleanup_connection(streamId, true);
    }

    void on_connection_error(uint64_t from, io::ErrorCode errorCode) override {
        //StreamId streamId(from);
        //LOG_WARNING() << "connection error " << io::error_str(errorCode) << " from " << streamId.address();
        //cleanup_connection(streamId, false);
    }

    void connect(io::Address address, OnConnectionStatus connectionCallback) override {
        // check connecting or connected

        _connectionCallback = connectionCallback;
        io::Reactor::Ptr reactor = io::Reactor::get_Current().shared_from_this();
        // TODO exceptions
        io::Result res = reactor->tcp_connect(address, uint64_t(this), BIND_THIS_MEMFN(on_connected), 10000);
        if (!res) {
            IO_EXCEPTION(res.error());
        } else {
            _connecting = true;
        }
    }

    void disconnect() override {
        if (_connecting) {
            io::Reactor::get_Current().cancel_tcp_connect(uint64_t(this));
            _connecting = false;
        }
        _connection.reset();
        _connectionCallback = OnConnectionStatus();
        _messageCallback = OnMessage();
        _peerListCallback = OnPeerList();
    }

    void publish(const void* buf, size_t size) override {
        Message msg;
        msg.bytes.assign(buf, size);
        _protocol.serialize(_msg, PUBLISH_MSG_TYPE, msg);
        io::Result res = _connection->write_msg(_msg);
        assert(res); //TODO
    }

    void request_peer_list(OnPeerList peerListCallback) override {
        _peerListCallback = peerListCallback;
        Request req;
        req.action = Request::get_servers;
        _protocol.serialize(_msg, REQUEST_MSG_TYPE, req);
        io::Result res = _connection->write_msg(_msg);
        assert(res); //TODO
    }

    void subscribe(OnMessage messageCallback, uint32_t historyDepthSec) override {
        get_history(messageCallback, historyDepthSec, 0);
    }

    void get_history(OnMessage messageCallback, uint32_t historyDepthStart, uint32_t historyDepthEnd) override {
        // TODO exceptions
        _messageCallback = messageCallback;
        Request req;
        req.action = Request::subscribe;
        req.startTimeDepth = historyDepthStart;
        req.endTimeDepth = historyDepthEnd;
        _protocol.serialize(_msg, REQUEST_MSG_TYPE, req);
        io::Result res = _connection->write_msg(_msg);
        assert(res); //TODO
    }

    void on_connected(uint64_t id, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
        if (errorCode == io::EC_OK) {
            _connection = std::make_unique<Connection>(
                _protocol,
                id,
                Connection::outbound,
                10000, //TODO config
                std::move(newStream)
            );
        }
        if (_connectionCallback) _connectionCallback(errorCode);
    }

    bool on_config_msg(uint64_t /*from*/, Config&& config) {
        _serverConfig = config;

        // TODO

        return true;
    }

    bool on_servers_msg(uint64_t /*from*/, Servers&& servers) {
        //for (const auto& p : servers.servers) {
        //    add_known_server(p.first, p.second);
        //}
        // TODO what spectre is needed
        if (_peerListCallback) _peerListCallback(servers);
        return true;
    }

    bool on_message(uint64_t /*from*/, Message&& message) {
        if (_messageCallback) _messageCallback(message);
        return true;
    }

    Protocol _protocol;

    OnConnectionStatus _connectionCallback;
    OnPeerList _peerListCallback;
    OnMessage _messageCallback;
    Connection::Ptr _connection;
    bool _connecting=false;
    Config _serverConfig;
    SerializedMsg _msg;
};

std::unique_ptr<Client> Client::create() {
    return std::make_unique<ClientImpl>();
}

}} //namespaces
