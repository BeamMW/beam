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
    enum WalletNetworkMessageCodes : uint8_t {
        senderInvitationCode     = 100,
        senderConfirmationCode   ,
        receiverConfirmationCode ,

        txRegisterCode           = 23,
        txRegisteredCode         = 5,
        txConfirmOutputCode      ,
        txOutputConfirmedCode    ,
        txFailedCode             
    };

    class WalletNetworkIO : public IMsgHandler
                          , public INetworkIO
                          , public proto::NodeConnection
    {
    public:

        using ConnectCallback = std::function<void(uint64_t tag)>;
        WalletNetworkIO(io::Address address
                      , io::Address node_address
                      , bool is_server
                      , IKeyChain::Ptr keychain
                      , io::Reactor::Ptr reactor = io::Reactor::Ptr()
                      , uint64_t start_tag = 0);
        
        void start();
        void stop();
        
        void send_money(io::Address receiver, Amount&& amount);
    private:
        // INetworkIO
        void send_tx_invitation(PeerId to, wallet::sender::InvitationData::Ptr&&) override;
        void send_tx_confirmation(PeerId to, wallet::sender::ConfirmationData::Ptr&&) override;
        void send_output_confirmation(PeerId to, None&&) override;
        void send_tx_confirmation(PeerId to, wallet::receiver::ConfirmationData::Ptr&&) override;
        void register_tx(Transaction::Ptr&&) override;
        void send_tx_registered(PeerId to, UuidPtr&& txId) override;

        // IMsgHandler
        void on_protocol_error(uint64_t fromStream, ProtocolError error) override;;
        void on_connection_error(uint64_t fromStream, int errorCode) override;

        // NodeConnection
        void OnConnected() override;
        void OnClosed(int errorCode) override;
        void OnMsg(proto::Boolean&& msg) override;

        // handlers for the protocol messages
        bool on_sender_inviatation(uint64_t connectionId, wallet::sender::InvitationData&& data);
        bool on_sender_confirmation(uint64_t connectionId, wallet::sender::ConfirmationData&& data);
        bool on_receiver_confirmation(uint64_t connectionId, wallet::receiver::ConfirmationData&& data);
        bool on_output_confirmation(uint64_t connectionId, None&& data);
        bool on_failed(uint64_t connectionId, Uuid&&);

        void connect_wallet(io::Address address, ConnectCallback&& callback);
        void on_stream_accepted(io::TcpStream::Ptr&& newStream, int errorCode);
        void on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, int status);
        bool register_connection(uint64_t tag, io::TcpStream::Ptr&& newStream);

        uint64_t get_connection_tag();
        
        template <typename T>
        void send(PeerId to, MsgType type, T&& data)
        {
            auto it = m_connections.find(to);
            if (it != m_connections.end()) {
                SerializedMsg msgToSend;
                m_protocol.serialize(msgToSend, type, data);
                it->second->write_msg(msgToSend); 
            }
            else {
                LOG_ERROR() << "No connection";
                // add some handling
            }
        }

    private:
        Protocol<WalletNetworkIO> m_protocol;
        io::Address m_address;
        io::Address m_node_address;
        io::Reactor::Ptr m_reactor;
        io::TcpServer::Ptr m_server;
        Wallet m_wallet;
        std::map<uint64_t, std::unique_ptr<Connection>> m_connections;
        std::map<uint64_t, ConnectCallback> m_connections_callbacks;
        std::vector<ConnectCallback> m_node_connections_callbacks;
        bool m_is_node_connected;
        uint64_t m_connection_tag;
    };
}