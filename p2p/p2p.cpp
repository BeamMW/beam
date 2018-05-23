#include "p2p.h"

namespace beam {

P2P::P2P(io::Address bindTo, uint16_t listenTo) :
    _protocol(0xAA, 0xBB, 0xCC, 22, *this, 0x2000),
    _ip(bindTo),
    _port(listenTo)
{
    //protocol.add_message_handler<Request, &NetworkSide::on_request>(requestCode, 1, 2000000);
    //protocol.add_message_handler<Response, &NetworkSide::on_response>(responseCode, 1, 200);
}

P2P::~P2P() {
    stop();
    wait();

}

void P2P::add_server(io::Address a) {

}

void P2P::start() {
    if (_port != 0) {
        _server = io::TcpServer::create(_reactor, io::Address(_ip, _port), BIND_THIS_MEMFN(on_stream_accepted));
    }
    connect_to_servers();
    run_async();
}

void P2P::connect_to_servers() {

}

} //namespace
