#include "wallet_network.h"

// protocol version
#define WALLET_MAJOR 0
#define WALLET_MINOR 0
#define WALLET_REV   1

using namespace std;

namespace beam {

    WalletNetworkIO::WalletNetworkIO(io::Address address
                                   , io::Address node_address
                                   , bool is_server
                                   , IKeyChain::Ptr keychain
                                   , io::Reactor::Ptr reactor
                                   , uint64_t start_tag)
        : m_protocol{ WALLET_MAJOR, WALLET_MINOR, WALLET_REV, *this, 200 }
        , m_address{address}
        , m_node_address{ node_address }
        , m_reactor{ !reactor ? io::Reactor::create() : reactor }
        , m_server{ is_server ? io::TcpServer::create(m_reactor, m_address, BIND_THIS_MEMFN(on_stream_accepted)) : io::TcpServer::Ptr() }
        , m_wallet{keychain, *this, is_server ? Wallet::TxCompletedAction() : [this](auto a) { this->stop(); } }
        , m_is_node_connected{false}
        , m_connection_tag{ start_tag }
        , m_node_connection{m_wallet}
    {
        m_protocol.add_message_handler<wallet::sender::InvitationData,     &WalletNetworkIO::on_sender_inviatation>(senderInvitationCode, 1, 2000);
        m_protocol.add_message_handler<wallet::sender::ConfirmationData,   &WalletNetworkIO::on_sender_confirmation>(senderConfirmationCode, 1, 2000);
        m_protocol.add_message_handler<wallet::receiver::ConfirmationData, &WalletNetworkIO::on_receiver_confirmation>(receiverConfirmationCode, 1, 2000);
        m_protocol.add_message_handler<None,                               &WalletNetworkIO::on_output_confirmation>(txOutputConfirmedCode, 1, 2000);
        m_protocol.add_message_handler<Uuid,                               &WalletNetworkIO::on_failed>(txFailedCode, 1, 2000);
    }

    void WalletNetworkIO::start()
    {
        m_reactor->run();
    }

    void WalletNetworkIO::stop()
    {
        m_reactor->stop();
    }

    void WalletNetworkIO::send_money(io::Address receiver, Amount&& amount)
    {
        connect_wallet(receiver, [this, amount = move(amount)](uint64_t tag) mutable {m_wallet.send_money(tag, move(amount)); });
    }

    void WalletNetworkIO::connect_wallet(io::Address address, ConnectCallback&& callback)
    {
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

    void WalletNetworkIO::register_tx(Transaction::Ptr&& data)
    {
        if (!m_is_node_connected)
        {
            m_node_connection.connect(m_node_address, [this, data=move(data)]()
            {
                m_node_connection.Send(proto::NewTransaction{ data });
            });
        }
        else
        {
            m_node_connection.Send(proto::NewTransaction{ data });
        }
    }

    void WalletNetworkIO::send_tx_registered(PeerId to, UuidPtr&& txId)
    {
        send(to, txRegisteredCode, *txId);
    }

    bool WalletNetworkIO::on_sender_inviatation(uint64_t connectionId, wallet::sender::InvitationData&& data)
    {
        m_wallet.handle_tx_invitation(connectionId, make_shared<wallet::sender::InvitationData>(move(data)));
        return true;
    }

    bool WalletNetworkIO::on_sender_confirmation(uint64_t connectionId, wallet::sender::ConfirmationData&& data)
    {
        m_wallet.handle_tx_confirmation(connectionId, make_shared<wallet::sender::ConfirmationData>(move(data)));
        return true;
    }

    bool WalletNetworkIO::on_receiver_confirmation(uint64_t connectionId, wallet::receiver::ConfirmationData&& data)
    {
        m_wallet.handle_tx_confirmation(connectionId, make_shared<wallet::receiver::ConfirmationData>(move(data)));
        return true;
    }

    bool WalletNetworkIO::on_output_confirmation(uint64_t connectionId, None&& data)
    {
        m_wallet.handle_output_confirmation(connectionId, move(data));
        return true;
    }

    bool WalletNetworkIO::on_failed(uint64_t connectionId, Uuid&& uuid)
    {
        m_wallet.handle_tx_failed(connectionId, make_shared<Uuid>(move(uuid)));
        return true;
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
        if (register_connection(tag, move(newStream))) {
            ConnectCallback callback;
            {
                assert(m_connections_callbacks.count(tag) == 1);
                callback = m_connections_callbacks[tag];
                m_connections_callbacks.erase(tag);
            }
            callback(tag);
        }
        else {
            on_connection_error(tag, status);
        }
    }

    bool WalletNetworkIO::register_connection(uint64_t tag, io::TcpStream::Ptr&& newStream)
    {
        auto it = m_connections.find(tag);
        if (it == m_connections.end() && newStream) {
            LOG_INFO() << "Connected to remote wallet";
            m_connections.emplace(tag, make_unique<Connection>(
                m_protocol,
                tag,
                100,
                std::move(newStream)
                ));
            return true;
        }
        return false;
    }

    void WalletNetworkIO::on_protocol_error(uint64_t fromStream, ProtocolError error)
    {
        LOG_ERROR() << __FUNCTION__ << "(" << fromStream << "," << error << ")";
    }

    void WalletNetworkIO::on_connection_error(uint64_t fromStream, int errorCode)
    {
        LOG_ERROR() << __FUNCTION__ << "(" << fromStream << "," << errorCode << ")";
        stop();
    }

    uint64_t WalletNetworkIO::get_connection_tag()
    {
        return ++m_connection_tag;
    }

    WalletNetworkIO::WalletNodeConnection::WalletNodeConnection(IWallet& wallet)
        : m_wallet {wallet}
    {
    }

    void WalletNetworkIO::WalletNodeConnection::connect(const io::Address& address, NodeConnectCallback&& cb)
    {
        m_connections_callbacks.emplace_back(move(cb));
        Connect(address);
    }

    void WalletNetworkIO::WalletNodeConnection::OnConnected()
    {
        if (!m_connections_callbacks.empty())
        {
            for (auto& cb : m_connections_callbacks)
            {
                cb();
            }
        }
    }

    void WalletNetworkIO::WalletNodeConnection::OnClosed(int errorCode)
    {
        m_connections_callbacks.clear();
    }

    void WalletNetworkIO::WalletNodeConnection::OnMsg(proto::Boolean&& msg)
    {
        m_wallet.handle_tx_registration(move(msg.m_Value));
    }

}