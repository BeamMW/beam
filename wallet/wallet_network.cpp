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
                                   , unsigned reconnect_ms
                                   , unsigned sync_period_ms
                                   , uint64_t start_tag)
        : m_protocol{ WALLET_MAJOR, WALLET_MINOR, WALLET_REV, 150, *this, 20000 }
        , m_address{address}
        , m_node_address{node_address}
        , m_reactor{ !reactor ? io::Reactor::create() : reactor }
        , m_server{ is_server ? io::TcpServer::create(m_reactor, m_address, BIND_THIS_MEMFN(on_stream_accepted)) : io::TcpServer::Ptr() }
        , m_wallet{keychain, *this, is_server ? Wallet::TxCompletedAction() : [this](auto ) { this->stop(); } }
        , m_is_node_connected{false}
        , m_connection_tag{ start_tag }
        , m_reactor_scope{*m_reactor }
        , m_reconnect_ms{ reconnect_ms }
        , m_sync_period_ms{ sync_period_ms }
        , m_sync_timer{io::Timer::create(m_reactor)}
    {
        m_protocol.add_message_handler<WalletNetworkIO, wallet::InviteReceiver,     &WalletNetworkIO::on_message>(senderInvitationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::ConfirmTransaction, &WalletNetworkIO::on_message>(senderConfirmationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::ConfirmInvitation,  &WalletNetworkIO::on_message>(receiverConfirmationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::TxRegistered,       &WalletNetworkIO::on_message>(receiverRegisteredCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::TxFailed,           &WalletNetworkIO::on_message>(failedCode, this, 1, 20000);

        connect_node();
    }

    WalletNetworkIO::~WalletNetworkIO()
    {
       // assert(m_connections.empty());
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

    Uuid WalletNetworkIO::transfer_money(io::Address receiver, Amount&& amount, ByteBuffer&& message)
    {
        auto tag = get_connection_tag();
        m_addresses.emplace(tag, receiver);
        return m_wallet.transfer_money(tag, move(amount), move(message));
    }

    void WalletNetworkIO::connect_wallet(io::Address address, uint64_t tag, ConnectCallback&& callback)
    {
        LOG_INFO() << "Establishing secure channel with " << address.str();
        m_connections_callbacks.emplace(tag, callback);
        auto res = m_reactor->tcp_connect(address, tag, BIND_THIS_MEMFN(on_client_connected));
        test_io_result(res);
    }

    void WalletNetworkIO::send_tx_message(PeerId to, wallet::InviteReceiver&& msg)
    {
        send(to, senderInvitationCode, move(msg));
    }

    void WalletNetworkIO::send_tx_message(PeerId to, wallet::ConfirmTransaction&& msg)
    {
        send(to, senderConfirmationCode, move(msg));
    }

    void WalletNetworkIO::send_tx_message(PeerId to, wallet::ConfirmInvitation&& msg)
    {
        send(to, receiverConfirmationCode, move(msg));
    }

    void WalletNetworkIO::send_tx_message(PeerId to, wallet::TxRegistered&& msg)
    {
        send(to, receiverRegisteredCode, move(msg));
    }

    void WalletNetworkIO::send_tx_message(PeerId to, wallet::TxFailed&& msg)
    {
        send(to, failedCode, move(msg));
    }

    void WalletNetworkIO::send_node_message(proto::NewTransaction&& msg)
    {
        send_to_node(move(msg));
    }

    void WalletNetworkIO::send_node_message(proto::GetProofUtxo&& msg)
    {
        send_to_node(move(msg));
    }

	void WalletNetworkIO::send_node_message(proto::GetHdr&& msg)
	{
		send_to_node(move(msg));
	}

    void WalletNetworkIO::send_node_message(proto::GetMined&& msg)
    {
        send_to_node(move(msg));
    }

    void WalletNetworkIO::close_connection(uint64_t id)
    {
        if (auto it = m_connections_callbacks.find(id); it != m_connections_callbacks.end())
        {
            m_connections_callbacks.erase(it);
            m_reactor->cancel_tcp_connect(id);
        }
        m_connections.erase(id);
    }

    void WalletNetworkIO::close_node_connection()
    {
        LOG_DEBUG() << "Close node connection";
        m_is_node_connected = false;
        m_node_connection.reset();
        start_sync_timer();
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::InviteReceiver&& msg)
    {
        m_wallet.handle_tx_message(connectionId, move(msg));
        return is_connected(connectionId);
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::ConfirmTransaction&& msg)
    {
        m_wallet.handle_tx_message(connectionId, move(msg));
        return is_connected(connectionId);
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::ConfirmInvitation&& msg)
    {
        m_wallet.handle_tx_message(connectionId, move(msg));
        return is_connected(connectionId);
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::TxRegistered&& msg)
    {
        m_wallet.handle_tx_message(connectionId, move(msg));
        return is_connected(connectionId);
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::TxFailed&& msg)
    {
        m_wallet.handle_tx_message(connectionId, move(msg));
        return is_connected(connectionId);
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
        if (m_is_node_connected == false && !m_node_connection)
        {
            create_node_connection();
            m_node_connection->connect(BIND_THIS_MEMFN(on_node_connected));
        }
    }

    void WalletNetworkIO::start_sync_timer()
    {
        m_sync_timer->start(m_sync_period_ms, false, BIND_THIS_MEMFN(on_sync_timer));
    }

    void WalletNetworkIO::on_sync_timer()
    {
        if (!m_is_node_connected)
        {
            connect_node();
        }
    }

    void WalletNetworkIO::on_node_connected()
    {
        m_is_node_connected = true;
    }

    void WalletNetworkIO::on_protocol_error(uint64_t from, ProtocolError error)
    {
        LOG_ERROR() << "Wallet protocol error: " << error;
        m_wallet.handle_connection_error(from);
        if (m_connections.empty())
        {
            stop();
            return;
        }
    }

    void WalletNetworkIO::on_connection_error(uint64_t from, io::ErrorCode errorCode)
    {
        LOG_ERROR() << "Wallet connection error: " << io::error_str(errorCode);

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

    void WalletNetworkIO::create_node_connection()
    {
        assert(!m_node_connection && !m_is_node_connected);
        m_node_connection = make_unique<WalletNodeConnection>(m_node_address, m_wallet, m_reactor, m_reconnect_ms);
    }

    void WalletNetworkIO::test_io_result(const io::Result res)
    {
        if (!res)
        {
            throw runtime_error(io::error_descr(res.error()));
        }
    }

    bool WalletNetworkIO::is_connected(uint64_t id)
    {
        return m_connections.find(id) != m_connections.end();
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
        LOG_DEBUG() << "Connecting to node...";
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
        proto::Config msgCfg = {};
		Rules::get_Hash(msgCfg.m_CfgChecksum);
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
        LOG_VERBOSE() << "Wallet failed to connect to node, error: " << io::error_str(io::ErrorCode(errorCode));
        m_wallet.stop_sync();
        m_timer->start(m_reconnectMsec, false, [this]() {Connect(m_address); });
    }

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::Boolean&& msg)
    {
        return m_wallet.handle_node_message(move(msg));
    }

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::ProofUtxo&& msg)
    {
        return m_wallet.handle_node_message(move(msg));
    }

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::NewTip&& msg)
	{
		return m_wallet.handle_node_message(move(msg));
	}

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::Hdr&& msg)
    {
        return m_wallet.handle_node_message(move(msg));
    }

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::Mined&& msg)
    {
        return m_wallet.handle_node_message(move(msg));
    }
}
