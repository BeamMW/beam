#pragma once

#include "p2p/protocol.h"
#include "p2p/connection.h"
#define LOG_DEBUG_ENABLED 1

#include "utility/bridge.h"
#include "utility/logger.h"
#include "utility/io/tcpserver.h"
#include "core/proto.h"
#include "utility/io/timer.h"
#include <boost/intrusive/set.hpp>
#include "wallet.h"

namespace beam
{
    namespace bi = boost::intrusive;

    enum WalletNetworkMessageCodes : uint8_t
    {
        senderInvitationCode     = 100,
        senderConfirmationCode   ,
        receiverConfirmationCode ,
        receiverRegisteredCode   ,
        failedCode
    };

    class WalletNetworkIO : public IErrorHandler
                          , public NetworkIOBase
    {
        struct ConnectionInfo;
        using ConnectCallback = std::function<void(const ConnectionInfo&)>;
    public:


        WalletNetworkIO(io::Address address
                      , io::Address node_address
                      , bool is_server
                      , IKeyChain::Ptr keychain
                      , io::Reactor::Ptr reactor = io::Reactor::Ptr()
                      , unsigned reconnect_ms = 1000 // 1 sec
                      , unsigned sync_period_ms = 60 * 1000  // 1 minute
                      , uint64_t start_tag = 0);
        virtual ~WalletNetworkIO();

        void start();
        void stop();

        void add_wallet(const WalletID& walletID, io::Address address);

    private:
        // INetworkIO
        void send_tx_message(const WalletID& to, wallet::Invite&&) override;
        void send_tx_message(const WalletID& to, wallet::ConfirmTransaction&&) override;
        void send_tx_message(const WalletID& to, wallet::ConfirmInvitation&&) override;
        void send_tx_message(const WalletID& to, wallet::TxRegistered&&) override;
        void send_tx_message(const WalletID& to, wallet::TxFailed&&) override;

        void send_node_message(proto::NewTransaction&&) override;
        void send_node_message(proto::GetProofUtxo&&) override;
        void send_node_message(proto::GetHdr&&) override;
        void send_node_message(proto::GetMined&&) override;
        void send_node_message(proto::GetProofState&&) override;

        void close_connection(const WalletID& id) override;
        void connect_node() override;
        void close_node_connection() override;

        // IMsgHandler
        void on_protocol_error(uint64_t fromStream, ProtocolError error) override;;
        void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) override;

        // handlers for the protocol messages
        bool on_message(uint64_t connectionId, wallet::Invite&& msg);
        bool on_message(uint64_t connectionId, wallet::ConfirmTransaction&& msg);
        bool on_message(uint64_t connectionId, wallet::ConfirmInvitation&& msg);
        bool on_message(uint64_t connectionId, wallet::TxRegistered&& msg);
        bool on_message(uint64_t connectionId, wallet::TxFailed&& msg);
        struct WalletInfo;
        void connect_wallet(const WalletInfo& wallet, uint64_t tag, ConnectCallback&& callback);
        void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);
        void on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode status);

        void start_sync_timer();
        void on_sync_timer();
        void on_node_connected();

        uint64_t get_connection_tag();
        void create_node_connection();
        void add_connection(uint64_t tag, ConnectionInfo&&);

        template <typename T>
        void send(const WalletID& walletID, MsgType type, T&& msg)
        {
            if (auto it = m_connectionWalletsIndex.find(walletID); it != m_connectionWalletsIndex.end())
            {
                if (it->m_connection)
                {
                    m_protocol.serialize(m_msgToSend, type, msg);
                    auto res = it->m_connection->write_msg(m_msgToSend);
                    m_msgToSend.clear();
                    test_io_result(res);
                }
            }
            else if (auto it = m_walletsIndex.find(walletID); it != m_walletsIndex.end())
            {
                auto t = std::make_shared<T>(std::move(msg)); // we need copyable object
                connect_wallet(*it, get_connection_tag(), [this, type, t](const ConnectionInfo& ci)
                {
                    send(ci.m_wallet.m_walletID, type, std::move(*t));
                });
            }
            else
            {
                throw std::runtime_error("failed to send message");
            }
        }

        template<typename T>
        void send_to_node(T&& msg)
        {
            if (!m_is_node_connected)
            {
                create_node_connection();
                m_node_connection->connect([this, msg=std::move(msg)]()
                {
                    m_is_node_connected = true;
                    m_node_connection->Send(msg);
                });
            }
            else
            {
                m_node_connection->Send(msg);
            }
        }

        void test_io_result(const io::Result res);
        bool is_connected(uint64_t id);

        const WalletID& get_wallet_id(uint64_t connectionId) const;
        uint64_t get_connection(const WalletID& peerID) const;

        class WalletNodeConnection : public proto::NodeConnection
        {
        public:
            using NodeConnectCallback = std::function<void()>;
            WalletNodeConnection(const io::Address& address, IWallet& wallet, io::Reactor::Ptr reactor, unsigned reconnectMsec);
            void connect(NodeConnectCallback&& cb);
        private:
            // NodeConnection
            void OnConnected() override;
			void OnDisconnect(const DisconnectReason&) override;
			bool OnMsg2(proto::Boolean&& msg) override;
            bool OnMsg2(proto::ProofUtxo&& msg) override;
            bool OnMsg2(proto::NewTip&& msg) override;
            bool OnMsg2(proto::Hdr&& msg) override;
            bool OnMsg2(proto::Mined&& msg) override;
            bool OnMsg2(proto::Proof&& msg) override;
        private:
            io::Address m_address;
            IWallet & m_wallet;
            std::vector<NodeConnectCallback> m_callbacks;
            bool m_connecting;
            io::Timer::Ptr m_timer;
            unsigned m_reconnectMsec;
        };

    private:

        Protocol m_protocol;
        WalletID m_walletID;
        io::Address m_node_address;
        io::Reactor::Ptr m_reactor;
        io::TcpServer::Ptr m_server;
        IWallet* m_wallet;

        struct WalletIDTag;
        struct AddressTag;

        using WalletIDHook = bi::set_base_hook <bi::tag<WalletIDTag>>;
        using AddressHook = bi::set_base_hook<bi::tag<AddressTag>>;
        
        struct WalletInfo : public WalletIDHook
                          , public AddressHook
        {
            WalletID m_walletID;
            io::Address m_address;
            WalletInfo(const WalletID& id, io::Address address)
                : m_walletID{id}
                , m_address{address}
            {}
        };

        struct WalletIDKey
        {
            typedef WalletID type;
            const WalletID& operator()(const WalletInfo& pi) const
            { 
                return pi.m_walletID; 
            }
        };

        struct AddressKey
        {
            typedef uint64_t type;
            uint64_t operator()(const WalletInfo& pi) const
            { 
                return pi.m_address.u64();
            }
        };

        struct ConnectionInfo : public WalletIDHook
        {
            uint64_t m_connectionID;
            const WalletInfo& m_wallet;
            ConnectCallback m_callback;
            std::unique_ptr<Connection> m_connection;

            ConnectionInfo(uint64_t id, const WalletInfo& wallet, ConnectCallback&& callback)
                : m_connectionID{ id }
                , m_wallet{ wallet }
                , m_callback{ std::move(callback) }
            {
            }

            bool operator<(const ConnectionInfo& other) const
            {
                return m_connectionID < other.m_connectionID;
            }
        };

        struct ConnectionWalletIDKey
        {
            typedef WalletID type;
            const WalletID& operator()(const ConnectionInfo& ci) const
            {
                return ci.m_wallet.m_walletID;
            }
        };

        std::vector<std::unique_ptr<WalletInfo>> m_wallets;
        std::map<uint64_t, ConnectionInfo> m_connections;
        bi::set<WalletInfo, bi::base_hook<WalletIDHook>, bi::key_of_value<WalletIDKey>> m_walletsIndex;
        bi::set<WalletInfo, bi::base_hook<AddressHook>, bi::key_of_value<AddressKey>> m_addressIndex;
        bi::set<ConnectionInfo, bi::base_hook<WalletIDHook>, bi::key_of_value<ConnectionWalletIDKey>> m_connectionWalletsIndex;

        bool m_is_node_connected;
        uint64_t m_connection_tag;
        io::Reactor::Scope m_reactor_scope;
        unsigned m_reconnect_ms;
        unsigned m_sync_period_ms;
        std::unique_ptr<WalletNodeConnection> m_node_connection;
        SerializedMsg m_msgToSend;
        io::Timer::Ptr m_sync_timer;
    };
}
