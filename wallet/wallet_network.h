#pragma once

#include "p2p/protocol.h"
#include "p2p/connection.h"
#define LOG_DEBUG_ENABLED 1

#include "utility/bridge.h"
#include "utility/logger.h"
#include "utility/io/tcpserver.h"
#include "core/proto.h"
#include "utility/io/timer.h"

#include "wallet.h"

namespace beam
{
    enum WalletNetworkMessageCodes : uint8_t
    {
        senderInvitationCode     = 100,
        senderConfirmationCode   ,
        receiverConfirmationCode ,
        receiverRegisteredCode   ,
        failedCode
    };

    class WalletNetworkIO : public IErrorHandler
                          , public INetworkIO
    {
    public:

        using ConnectCallback = std::function<void(uint64_t tag)>;

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

        Uuid transfer_money(io::Address receiver, Amount&& amount, Amount&& fee = 0, bool sender = true, ByteBuffer&& message = {});

    private:
        // INetworkIO
        void send_tx_message(PeerId to, wallet::Invite&&) override;
        void send_tx_message(PeerId to, wallet::ConfirmTransaction&&) override;
        void send_tx_message(PeerId to, wallet::ConfirmInvitation&&) override;
        void send_tx_message(PeerId to, wallet::TxRegistered&&) override;
        void send_tx_message(PeerId to, wallet::TxFailed&&) override;

        void send_node_message(proto::NewTransaction&&) override;
        void send_node_message(proto::GetProofUtxo&&) override;
        void send_node_message(proto::GetHdr&&) override;
        void send_node_message(proto::GetMined&&) override;
        void send_node_message(proto::GetProofState&&) override;

        void close_connection(uint64_t id) override;
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

        void connect_wallet(io::Address address, uint64_t tag, ConnectCallback&& callback);
        void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);
        void on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode status);
        bool register_connection(uint64_t tag, io::TcpStream::Ptr&& newStream);

        void connect_node();
        void start_sync_timer();
        void on_sync_timer();
        void on_node_connected();

        uint64_t get_connection_tag();
        void create_node_connection();

        template <typename T>
        void send(PeerId to, MsgType type, T&& msg)
        {
            if (auto it = m_connections.find(to); it != m_connections.end())
            {
                m_protocol.serialize(m_msgToSend, type, msg);
                auto res = it->second->write_msg(m_msgToSend);
                m_msgToSend.clear();
                test_io_result(res);
            }
            else if (auto it = m_addresses.find(to); it != m_addresses.end())
            {
                auto t = std::make_shared<T>(std::move(msg)); // we need copyable object
                connect_wallet(it->second, to, [this, type, t](uint64_t tag)
                {
                    send(tag, type, std::move(*t));
                });
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

        class WalletNodeConnection : public proto::NodeConnection
        {
        public:
            using NodeConnectCallback = std::function<void()>;
            WalletNodeConnection(const io::Address& address, IWallet& wallet, io::Reactor::Ptr reactor, unsigned reconnectMsec);
            void connect(NodeConnectCallback&& cb);
        private:
            // NodeConnection
            void OnConnected() override;
            void OnClosed(int errorCode) override;
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
        io::Address m_address;
        io::Address m_node_address;
        io::Reactor::Ptr m_reactor;
        io::TcpServer::Ptr m_server;
        Wallet m_wallet;
        std::unordered_map<uint64_t, io::Address> m_addresses;
        std::map<uint64_t, std::unique_ptr<Connection>> m_connections;
        std::map<uint64_t, ConnectCallback> m_connections_callbacks;
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
