#pragma once

#include "p2p/protocol.h"
#include "p2p/connection.h"
#define LOG_DEBUG_ENABLED 1

#include "utility/bridge.h"
#include "utility/logger.h"
#include "utility/io/tcpserver.h"
#include "core/proto.h"
//#include "utility/io/timer.h"

#include "wallet.h"

namespace beam
{
    enum WalletNetworkMessageCodes : uint8_t
    {
        senderInvitationCode     = 100,
        senderConfirmationCode   ,
        receiverConfirmationCode ,
        receiverRegisteredCode   ,
    };

    class WalletNetworkIO : public IMsgHandler
                          , public INetworkIO
    {
    public:

        using ConnectCallback = std::function<void(uint64_t tag)>;
        WalletNetworkIO(io::Address address
                      , io::Address node_address
                      , bool is_server
                      , IKeyChain::Ptr keychain
                      , io::Reactor::Ptr reactor = io::Reactor::Ptr()
                      , uint64_t start_tag = 0);
        virtual ~WalletNetworkIO();
        
        void start();
        void stop();
        
        void transfer_money(io::Address receiver, Amount&& amount);
        
    private:
        // INetworkIO
        void send_tx_message(PeerId to, wallet::sender::InvitationData::Ptr&&) override;
        void send_tx_message(PeerId to, wallet::sender::ConfirmationData::Ptr&&) override;
        void send_tx_message(PeerId to, wallet::receiver::ConfirmationData::Ptr&&) override;
        void send_tx_message(PeerId to, wallet::TxRegisteredData&&) override;
        void send_node_message(proto::NewTransaction&&) override;
		void send_node_message(proto::GetProofUtxo&&) override;
        void send_node_message(proto::GetHdr&&) override;

        void close_connection(uint64_t id) override;

        // IMsgHandler
        void on_protocol_error(uint64_t fromStream, ProtocolError error) override;;
        void on_connection_error(uint64_t fromStream, int errorCode) override;

        // handlers for the protocol messages
        bool on_message(uint64_t connectionId, wallet::sender::InvitationData&& data);
        bool on_message(uint64_t connectionId, wallet::sender::ConfirmationData&& data);
        bool on_message(uint64_t connectionId, wallet::receiver::ConfirmationData&& data);
        bool on_message(uint64_t connectionId, wallet::TxRegisteredData&& data);

        void connect_wallet(io::Address address, ConnectCallback&& callback);
        void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);
        void on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode status);
        bool register_connection(uint64_t tag, io::TcpStream::Ptr&& newStream);

        uint64_t get_connection_tag();
        
        template <typename T>
        void send(PeerId to, MsgType type, const T& data)
        {
            auto it = m_connections.find(to);
            if (it != m_connections.end())
            {
                m_protocol.serialize(m_msgToSend, type, data);
                it->second->write_msg(m_msgToSend);
                m_msgToSend.clear();
            }
            else
            {
                LOG_ERROR() << "No connection";
                // add some handling
            }
        }

        template<typename T>
        void send_to_node(T&& msg)
        {
            if (!m_is_node_connected)
            {
                m_node_connection.connect(m_node_address, [this, msg=std::move(msg)]()
                {
                    m_is_node_connected = true;
                    m_node_connection.Send(msg);
                });
            }
            else
            {
                m_node_connection.Send(msg);
            }
        }

        class WalletNodeConnection : public proto::NodeConnection
        {
        public:
            using NodeConnectCallback = std::function<void()>;
            WalletNodeConnection(IWallet& wallet);
            void connect(const io::Address& address, NodeConnectCallback&& cb);
        private:
            // NodeConnection
            void OnConnected() override;
            void OnClosed(int errorCode) override;
            void OnMsg(proto::Boolean&& msg) override;
            void OnMsg(proto::ProofUtxo&& msg) override;
			void OnMsg(proto::NewTip&& msg) override;
        private:
            IWallet & m_wallet;
            std::vector<NodeConnectCallback> m_connections_callbacks;
        };
    
    private:
        Protocol<WalletNetworkIO> m_protocol;
        io::Address m_address;
        io::Address m_node_address;
        io::Reactor::Ptr m_reactor;
        io::TcpServer::Ptr m_server;
        Wallet m_wallet;
        std::map<uint64_t, std::unique_ptr<Connection>> m_connections;
        std::map<uint64_t, ConnectCallback> m_connections_callbacks;
        bool m_is_node_connected;
        uint64_t m_connection_tag;
        WalletNodeConnection m_node_connection;
        SerializedMsg m_msgToSend;
    };
}