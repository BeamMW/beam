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
                                   , unsigned reconnectMsec
                                   , uint64_t start_tag)
        : m_protocol{ WALLET_MAJOR, WALLET_MINOR, WALLET_REV, 150, *this, 20000 }
        , m_address{address}
        , m_reactor{ !reactor ? io::Reactor::create() : reactor }
        , m_server{ is_server ? io::TcpServer::create(m_reactor, m_address, BIND_THIS_MEMFN(on_stream_accepted)) : io::TcpServer::Ptr() }
        , m_wallet{keychain, *this, is_server ? Wallet::TxCompletedAction() : [this](auto a) { this->stop(); } }
        , m_is_node_connected{false}
        , m_connection_tag{ start_tag }
        , m_reactor_scope{*m_reactor }
        , m_node_connection{node_address, m_wallet, m_reactor, reconnectMsec }
    {
        m_protocol.add_message_handler<WalletNetworkIO, wallet::sender::InvitationData,     &WalletNetworkIO::on_message>(senderInvitationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::sender::ConfirmationData,   &WalletNetworkIO::on_message>(senderConfirmationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::receiver::ConfirmationData, &WalletNetworkIO::on_message>(receiverConfirmationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::TxRegisteredData,           &WalletNetworkIO::on_message>(receiverRegisteredCode, this, 1, 20000);

        connect_node();
    }

    WalletNetworkIO::~WalletNetworkIO()
    {
        assert(m_connections.empty());
        assert(m_connections_callbacks.empty());
    }

    void WalletNetworkIO::start()
    {
        m_reactor->run();
    }

    void WalletNetworkIO::stop()
    {
        m_reactor->stop();
    }

    void WalletNetworkIO::transfer_money(io::Address receiver, Amount&& amount)
    {
        connect_wallet(receiver, [this, receiver, amount = move(amount)](uint64_t tag) mutable
        {
            m_wallet.transfer_money(tag, move(amount));
        });
    }

    void WalletNetworkIO::connect_wallet(io::Address address, ConnectCallback&& callback)
    {
        LOG_INFO() << "Establishing secure channel with " << address.str();
        auto tag = get_connection_tag();
        m_connections_callbacks.emplace(tag, callback);
        m_reactor->tcp_connect(address, tag, BIND_THIS_MEMFN(on_client_connected));
    }

    void WalletNetworkIO::send_tx_message(PeerId to, wallet::sender::InvitationData::Ptr&& data)
    {
        send(to, senderInvitationCode, *data);
    }

    void WalletNetworkIO::send_tx_message(PeerId to, wallet::sender::ConfirmationData::Ptr&& data)
    {
        send(to, senderConfirmationCode, *data);
    }

    void WalletNetworkIO::send_tx_message(PeerId to, wallet::receiver::ConfirmationData::Ptr&& data)
    {
        send(to, receiverConfirmationCode, *data);
    }

    void WalletNetworkIO::send_tx_message(PeerId to, wallet::TxRegisteredData&& data)
    {
        send(to, receiverRegisteredCode, move(data));
    }

    void WalletNetworkIO::send_node_message(proto::NewTransaction&& data)
    {
        send_to_node(move(data));
    }

    void WalletNetworkIO::send_node_message(proto::GetProofUtxo&& data)
    {
        send_to_node(move(data));
    }

	void WalletNetworkIO::send_node_message(proto::GetHdr&& data)
	{
		send_to_node(move(data));
	}

    void WalletNetworkIO::send_node_message(proto::GetMined&& data)
    {
        send_to_node(move(data));
    }

    void WalletNetworkIO::close_connection(uint64_t id)
    {
        m_connections.erase(id);
        if (auto it = m_connections_callbacks.find(id); it != m_connections_callbacks.end())
        {
            m_connections_callbacks.erase(it);
            m_reactor->cancel_tcp_connect(id);
        }
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::sender::InvitationData&& data)
    {
        m_wallet.handle_tx_message(connectionId, make_shared<wallet::sender::InvitationData>(move(data)));
        return true;
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::sender::ConfirmationData&& data)
    {
        m_wallet.handle_tx_message(connectionId, make_shared<wallet::sender::ConfirmationData>(move(data)));
        return true;
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::receiver::ConfirmationData&& data)
    {
        m_wallet.handle_tx_message(connectionId, make_shared<wallet::receiver::ConfirmationData>(move(data)));
        return true;
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::TxRegisteredData&& data)
    {
        m_wallet.handle_tx_message(connectionId, move(data));
        return true;
    }

    void WalletNetworkIO::on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
    {
        if (errorCode == 0)
        {
            LOG_DEBUG() << "Wallet connected: " << newStream->peer_address();
            auto tag = get_connection_tag();
            m_connections.emplace(tag,
                make_unique<Connection>(
                    m_protocol,
                    tag,
                    Connection::outbound,
                    2000,
                    std::move(newStream)));
        }
        else
        {
            on_connection_error(m_address.u64(), errorCode);
        }
    }

    void WalletNetworkIO::on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode status)
    {
        if (register_connection(tag, move(newStream)))
        {
            ConnectCallback callback;
            assert(m_connections_callbacks.count(tag) == 1);
            callback = m_connections_callbacks[tag];
            m_connections_callbacks.erase(tag);
            callback(tag);
        }
        else
        {
            m_connections_callbacks.erase(tag);
            on_connection_error(tag, status);
        }
    }

    bool WalletNetworkIO::register_connection(uint64_t tag, io::TcpStream::Ptr&& newStream)
    {
        auto it = m_connections.find(tag);
        if (it == m_connections.end() && newStream)
        {
            LOG_INFO() << "Connected to remote wallet: " << newStream->peer_address();
            m_connections.emplace(tag, make_unique<Connection>(
                m_protocol,
                tag,
                Connection::outbound,
                2000,
                std::move(newStream)
                ));
            return true;
        }
        return false;
    }

    void WalletNetworkIO::connect_node()
    {
        m_node_connection.connect(BIND_THIS_MEMFN(on_node_connected));
    }

    void WalletNetworkIO::on_node_connected()
    {
         m_is_node_connected = true;
    }

    void WalletNetworkIO::on_protocol_error(uint64_t from, ProtocolError error)
    {
        LOG_ERROR() << "Failed to connect to remote wallet: " << error;
        m_wallet.handle_connection_error(from);
        if (m_connections.empty())
        {
            stop();
            return;
        }
    }

    void WalletNetworkIO::on_connection_error(uint64_t from, int errorCode)
    {
        LOG_ERROR() << "Failed to connect to remote wallet: " << errorCode;
        if (m_connections.empty())
        {
            stop();
            return;
        }
        m_wallet.handle_connection_error(from);
    }

    uint64_t WalletNetworkIO::get_connection_tag()
    {
        return ++m_connection_tag;
    }

    WalletNetworkIO::WalletNodeConnection::WalletNodeConnection(const io::Address& address, IWallet& wallet, io::Reactor::Ptr reactor, unsigned reconnectMsec)
        : m_address{address}
        , m_wallet {wallet}
        , m_connecting{false}
        , m_timer{io::Timer::create(reactor)}
        , m_reconnectMsec{reconnectMsec}
    {
    }

    void WalletNetworkIO::WalletNodeConnection::connect(NodeConnectCallback&& cb)
    {
        m_callbacks.emplace_back(move(cb));
        if (!m_connecting)
        {
            Connect(m_address);
        }
    }

    void WalletNetworkIO::WalletNodeConnection::OnConnected()
    {
        LOG_INFO() << "Wallet connected to node";
        m_connecting = false;
        proto::Config msgCfg = {0};
		msgCfg.m_AutoSendHdr = true;
		Send(msgCfg);

        for (auto& cb : m_callbacks)
        {
            cb();
        }
        m_callbacks.clear();
    }

    void WalletNetworkIO::WalletNodeConnection::OnClosed(int errorCode)
    {
        LOG_INFO() << "Could not connect to node, retrying...";
        LOG_VERBOSE() << "Wallet failed to connect to node, error: " << errorCode;
        m_timer->start(m_reconnectMsec, false, [this]() {Connect(m_address); });
    }

    void WalletNetworkIO::WalletNodeConnection::OnMsg(proto::Boolean&& msg)
    {
        m_wallet.handle_node_message(move(msg));
    }

    void WalletNetworkIO::WalletNodeConnection::OnMsg(proto::ProofUtxo&& msg)
    {
        m_wallet.handle_node_message(move(msg));
    }

	void WalletNetworkIO::WalletNodeConnection::OnMsg(proto::NewTip&& msg)
	{
		m_wallet.handle_node_message(move(msg));
	}

    void WalletNetworkIO::WalletNodeConnection::OnMsg(proto::Hdr&& msg)
    {
        m_wallet.handle_node_message(move(msg));
    }

    void WalletNetworkIO::WalletNodeConnection::OnMsg(proto::Mined&& msg)
    {
        m_wallet.handle_node_message(move(msg));
    }
}
