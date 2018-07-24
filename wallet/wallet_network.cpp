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
        , m_node_address{node_address}
        , m_reactor{ !reactor ? io::Reactor::create() : reactor }
        , m_server{ is_server ? io::TcpServer::create(m_reactor, address, BIND_THIS_MEMFN(on_stream_accepted)) : io::TcpServer::Ptr() }
        , m_wallet{ nullptr }
        , m_keychain{keychain}
        , m_is_node_connected{false}
        , m_connection_tag{ start_tag }
        , m_reactor_scope{*m_reactor }
        , m_reconnect_ms{ reconnect_ms }
        , m_sync_period_ms{ sync_period_ms }
        , m_sync_timer{io::Timer::create(m_reactor)}
    {
        m_protocol.add_message_handler<WalletNetworkIO, wallet::Invite,             &WalletNetworkIO::on_message>(senderInvitationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::ConfirmTransaction, &WalletNetworkIO::on_message>(senderConfirmationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::ConfirmInvitation,  &WalletNetworkIO::on_message>(receiverConfirmationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::TxRegistered,       &WalletNetworkIO::on_message>(receiverRegisteredCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::TxFailed,           &WalletNetworkIO::on_message>(failedCode, this, 1, 20000);
    }

    WalletNetworkIO::~WalletNetworkIO()
    {
        assert(m_connections.empty());
        assert(m_connectionWalletsIndex.empty());
    }

    void WalletNetworkIO::start()
    {
        m_reactor->run();
    }

    void WalletNetworkIO::stop()
    {
        m_reactor->stop();
    }

    void WalletNetworkIO::add_wallet(const WalletID& walletID, io::Address address)
    {
        m_wallets.push_back(make_unique<WalletInfo>(walletID, address));
        auto& t = m_wallets.back();
        m_walletsIndex.insert(*t);
        m_addressIndex.insert(*t);
    }

    void WalletNetworkIO::connect_wallet(const WalletInfo& wallet, uint64_t tag, ConnectCallback&& callback)
    {
        LOG_INFO() << "Establishing secure channel with " << wallet.m_address.str();
        add_connection(tag, ConnectionInfo{ tag, wallet, move(callback) });
        auto res = m_reactor->tcp_connect(wallet.m_address, tag, BIND_THIS_MEMFN(on_client_connected));
        test_io_result(res);
    }

    void WalletNetworkIO::send_tx_message(const WalletID& to, wallet::Invite&& msg)
    {
        send(to, senderInvitationCode, move(msg));
    }

    void WalletNetworkIO::send_tx_message(const WalletID& to, wallet::ConfirmTransaction&& msg)
    {
        send(to, senderConfirmationCode, move(msg));
    }

    void WalletNetworkIO::send_tx_message(const WalletID& to, wallet::ConfirmInvitation&& msg)
    {
        send(to, receiverConfirmationCode, move(msg));
    }

    void WalletNetworkIO::send_tx_message(const WalletID& to, wallet::TxRegistered&& msg)
    {
        send(to, receiverRegisteredCode, move(msg));
    }

    void WalletNetworkIO::send_tx_message(const WalletID& to, wallet::TxFailed&& msg)
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

    void WalletNetworkIO::send_node_message(proto::GetProofState&& msg)
    {
        send_to_node(move(msg));
    }

    void WalletNetworkIO::close_connection(const WalletID& walletID)
    {
        if (auto it = m_connectionWalletsIndex.find(walletID, ConnectionWalletIDComparer()); it != m_connectionWalletsIndex.end())
        {
            auto& ci = *it;
            if (ci.m_callback)
            {
                ci.m_callback = {};
                m_reactor->cancel_tcp_connect(ci.m_connectionID);
            }
            
            m_connectionWalletsIndex.erase(it);
            m_connections.erase(ci.m_connectionID);
        }
    }

    void WalletNetworkIO::close_node_connection()
    {
        LOG_DEBUG() << "Close node connection";
        m_is_node_connected = false;
        m_node_connection.reset();
        start_sync_timer();
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::Invite&& msg)
    {
        get_wallet().handle_tx_message(get_wallet_id(connectionId), move(msg));
        return is_connected(connectionId);
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::ConfirmTransaction&& msg)
    {
        get_wallet().handle_tx_message(get_wallet_id(connectionId), move(msg));
        return is_connected(connectionId);
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::ConfirmInvitation&& msg)
    {
        get_wallet().handle_tx_message(get_wallet_id(connectionId), move(msg));
        return is_connected(connectionId);
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::TxRegistered&& msg)
    {
        get_wallet().handle_tx_message(get_wallet_id(connectionId), move(msg));
        return is_connected(connectionId);
    }

    bool WalletNetworkIO::on_message(uint64_t connectionId, wallet::TxFailed&& msg)
    {
        get_wallet().handle_tx_message(get_wallet_id(connectionId), move(msg));
        return is_connected(connectionId);
    }

    void WalletNetworkIO::on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
    {
        if (errorCode == 0)
        {
            io::Address address = newStream->peer_address();
            LOG_DEBUG() << "Wallet connected: " << address;

            auto it = m_addressIndex.find(address.u64(), AddressComparer());
            if (it == m_addressIndex.end())
            {
                WalletID id = {};
                id = address.u64();
                add_wallet(id, address);
                it = m_addressIndex.find(address.u64(), AddressComparer());
            }

            auto tag = get_connection_tag();

            ConnectionInfo ci(tag, *it, {});
            ci.m_connection = make_unique<Connection>(
                                        m_protocol,
                                        tag,
                                        Connection::outbound,
                                        2000,
                                        std::move(newStream));
            add_connection(tag, move(ci));
        }
        else
        {
            on_connection_error(0, errorCode);
        }
    }

    void WalletNetworkIO::on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode status)
    {
        if (auto it = m_connections.find(tag); it != m_connections.end() && newStream)
        {
            ConnectionInfo& ci = it->second;
            LOG_INFO() << "Connected to remote wallet: " << newStream->peer_address();
            ci.m_connection = make_unique<Connection>(
                m_protocol,
                tag,
                Connection::outbound,
                2000,
                std::move(newStream));
            if (ci.m_callback)
            {
                ConnectCallback callback = ci.m_callback;
                ci.m_callback = {};
                callback(ci);
            }
        }
        else
        {
            on_connection_error(tag, status);
        }
    }

    void WalletNetworkIO::connect_node()
    {
        if (m_is_node_connected == false && !m_node_connection)
        {
			m_sync_timer->cancel();

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
        //get_wallet().handle_connection_error(from);
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
        //get_wallet().handle_connection_error(from);
    }

    uint64_t WalletNetworkIO::get_connection_tag()
    {
        return ++m_connection_tag;
    }

    void WalletNetworkIO::create_node_connection()
    {
        assert(!m_node_connection && !m_is_node_connected);
        m_node_connection = make_unique<WalletNodeConnection>(m_node_address, get_wallet(), m_reactor, m_reconnect_ms);
    }

    void WalletNetworkIO::add_connection(uint64_t tag, ConnectionInfo&& ci)
    {
        auto p = m_connections.emplace(tag, move(ci));
        m_connectionWalletsIndex.insert(p.first->second);
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
        auto it = m_connections.find(id);
        return it != m_connections.end() && it->second.m_connection;
    }

    const WalletID& WalletNetworkIO::get_wallet_id(uint64_t connectionId) const
    {
        auto it = m_connections.find(connectionId);
        if (it == m_connections.end())
        {
            throw runtime_error("Unknown connection");
        }
        return it->second.m_wallet.m_walletID;
    }

    uint64_t WalletNetworkIO::get_connection(const WalletID& walletID) const
    {
        auto it = m_connectionWalletsIndex.find(walletID, ConnectionWalletIDComparer());
        if (it == m_connectionWalletsIndex.end())
        {
            throw runtime_error("Unknown walletID");
        }
        return it->m_connectionID;
    }

    void WalletNetworkIO::update_wallets(const WalletID& walletID)
    {
        auto p = m_keychain->getPeer(walletID);
        if (p.is_initialized())
        {
            io::Address address;
            if (address.resolve(p->m_address.c_str()))
            {
                add_wallet(p->m_walletID, address);
            }
        }
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
        proto::Config msgCfg;
		ZeroObject(msgCfg);
		msgCfg.m_CfgChecksum = Rules::get().Checksum;
		msgCfg.m_AutoSendHdr = true;
		Send(msgCfg);

        for (auto& cb : m_callbacks)
        {
            cb();
        }
        m_callbacks.clear();
    }

    void WalletNetworkIO::WalletNodeConnection::OnDisconnect(const DisconnectReason& r)
    {
        LOG_INFO() << "Could not connect to node, retrying...";
        LOG_VERBOSE() << "Wallet failed to connect to node, error: " << r;
        m_wallet.abort_sync();
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

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::Proof&& msg)
    {
        return m_wallet.handle_node_message(move(msg));
    }
}
