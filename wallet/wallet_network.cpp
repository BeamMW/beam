#include "wallet_network.h"

// protocol version
#define WALLET_MAJOR 0
#define WALLET_MINOR 0
#define WALLET_REV   1

using namespace std;

namespace beam {
    WalletNetworkIO::WalletNetworkIO(io::Address address)
        : m_protocol{ WALLET_MAJOR, WALLET_MINOR, WALLET_REV, *this, 200 }
        , m_address{address}
        , m_reactor{ io::Reactor::create() }
        , m_bridge{*this, m_reactor }
        , m_wallet{nullptr}
        , m_connection_tag{0}
    {
        m_protocol.add_message_handler<wallet::sender::InvitationData,     &WalletNetworkIO::on_sender_inviatation>(senderInvitationCode, 1, 2000);
        m_protocol.add_message_handler<wallet::sender::ConfirmationData,   &WalletNetworkIO::on_sender_confirmation>(senderConfirmationCode, 1, 2000);
        m_protocol.add_message_handler<wallet::receiver::ConfirmationData, &WalletNetworkIO::on_receiver_confirmation>(receiverConfirmationCode, 1, 2000);
        m_protocol.add_message_handler<None,                               &WalletNetworkIO::on_output_confirmation>(txOutputConfirmedCode, 1, 2000);
        m_protocol.add_message_handler<Uuid,                               &WalletNetworkIO::on_registration>(txRegisteredCode, 1, 2000);
        m_protocol.add_message_handler<Uuid,                               &WalletNetworkIO::on_failed>(txFailedCode, 1, 2000);
    }

    void WalletNetworkIO::start()
    {
        if (!m_server) {
            m_server = io::TcpServer::create(m_reactor, m_address, BIND_THIS_MEMFN(on_stream_accepted));
        }
        m_thread.start(BIND_THIS_MEMFN(thread_func));
    }

    void WalletNetworkIO::stop()
    {
        m_reactor->stop();
        m_thread.join();
    }

    void WalletNetworkIO::wait()
    {
        m_thread.join();
    }

    INetworkIO& WalletNetworkIO::get_network_proxy()
    {
        return m_bridge;
    }

    void WalletNetworkIO::set_wallet_proxy(IWallet* wallet)
    {
        assert(wallet != nullptr && m_wallet == nullptr);
        m_wallet = wallet;
    }

    void WalletNetworkIO::connect(io::Address address, ConnectCallback&& callback)
    {
        assert(m_wallet != nullptr);
        auto tag = get_connection_tag();
        m_connections_callbacks.emplace(tag, callback);
        m_reactor->tcp_connect(address, tag, BIND_THIS_MEMFN(on_client_connected));
    }

    void WalletNetworkIO::send_tx_invitation(PeerId to, wallet::sender::InvitationData::Ptr&& data)
    {
        send(to, senderInvitationCode, *data);
    }

    void WalletNetworkIO::send_tx_confirmation(PeerId to, wallet::sender::ConfirmationData::Ptr&& data)
    {
        send(to, senderConfirmationCode, *data);
    }

    void WalletNetworkIO::send_output_confirmation(PeerId to, None&& data)
    {
        send(to, txConfirmOutputCode, data);
    }

    void WalletNetworkIO::send_tx_confirmation(PeerId to, wallet::receiver::ConfirmationData::Ptr&& data)
    {
        send(to, receiverConfirmationCode, *data);
    }

    void WalletNetworkIO::register_tx(PeerId to, wallet::receiver::RegisterTxData::Ptr&& data)
    {
        send(to, txRegisterCode, *data);
    }

    void WalletNetworkIO::send_tx_registered(PeerId to, UuidPtr&& txId)
    {
        send(to, txRegisteredCode, *txId);
    }

    bool WalletNetworkIO::on_sender_inviatation(uint64_t connectionId, wallet::sender::InvitationData&& data)
    {
        assert(m_wallet != nullptr);
        m_wallet->handle_tx_invitation(connectionId, make_shared<wallet::sender::InvitationData>(move(data)));
        return true;
    }

    bool WalletNetworkIO::on_sender_confirmation(uint64_t connectionId, wallet::sender::ConfirmationData&& data)
    {
        assert(m_wallet != nullptr);
        m_wallet->handle_tx_confirmation(connectionId, make_shared<wallet::sender::ConfirmationData>(move(data)));
        return true;
    }

    bool WalletNetworkIO::on_receiver_confirmation(uint64_t connectionId, wallet::receiver::ConfirmationData&& data)
    {
        assert(m_wallet != nullptr);
        m_wallet->handle_tx_confirmation(connectionId, make_shared<wallet::receiver::ConfirmationData>(move(data)));
        return true;
    }

    bool WalletNetworkIO::on_output_confirmation(uint64_t connectionId, None&& data)
    {
        assert(m_wallet != nullptr);
        m_wallet->handle_output_confirmation(connectionId, move(data));
        return true;
    }

    bool WalletNetworkIO::on_registration(uint64_t connectionId, Uuid&& uuid)
    {
        assert(m_wallet != nullptr);
        m_wallet->handle_tx_registration(connectionId, make_shared<Uuid>(move(uuid)));
        return true;
    }

    bool WalletNetworkIO::on_failed(uint64_t connectionId, Uuid&& uuid)
    {
        assert(m_wallet != nullptr);
        m_wallet->handle_tx_failed(connectionId, make_shared<Uuid>(move(uuid)));
        return true;
    }

    void WalletNetworkIO::thread_func()
    {
        LOG_INFO() << __PRETTY_FUNCTION__ << " starting";
        m_reactor->run();
        LOG_DEBUG() << __PRETTY_FUNCTION__ << " exiting";
        m_bridge.stop_rx();
    }

    void WalletNetworkIO::on_stream_accepted(io::TcpStream::Ptr&& newStream, int errorCode)
    {
        if (errorCode == 0) {
            LOG_DEBUG() << "Stream accepted";
            auto tag = get_connection_tag();
            m_connections.emplace(tag,
                make_unique<Connection>(
                    m_protocol,
                    tag,
                    100,
                    std::move(newStream)));
        }
        else {
            on_connection_error(m_address.packed, errorCode);
        }
    }

    void WalletNetworkIO::on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, int status)
    {
        //assert(tag == m_address.packed);
        auto it = m_connections.find(tag);
        
        if (it == m_connections.end() && newStream) {
            m_connections.emplace(tag, make_unique<Connection>(
                m_protocol,
                tag,
                100,
                std::move(newStream)
                ));
            m_connections_callbacks[tag](tag, status);
            m_connections_callbacks.erase(tag);
        }
        else {
            on_connection_error(tag, status);
        }
    }

    void WalletNetworkIO::on_protocol_error(uint64_t fromStream, ProtocolError error)
    {
        LOG_ERROR() << __FUNCTION__ << "(" << fromStream << "," << error << ")";
    }

    void WalletNetworkIO::on_connection_error(uint64_t fromStream, int errorCode)
    {
        LOG_ERROR() << __FUNCTION__ << "(" << fromStream << "," << errorCode << ")";
    }

    uint64_t WalletNetworkIO::get_connection_tag()
    {
        return ++m_connection_tag;
    }
}