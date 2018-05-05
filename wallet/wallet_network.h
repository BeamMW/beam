#pragma once

#include "p2p/protocol.h"
#include "p2p/connection.h"
#define LOG_DEBUG_ENABLED 1

#include "utility/bridge.h"
#include "utility/logger.h"
#include "utility/io/tcpserver.h"
//#include "utility/io/timer.h"

#include "wallet.h"

namespace beam
{
    enum WalletNetworkMessageCodes : uint8_t {
        senderInvitationCode     = 3,
        senderConfirmationCode   = 4,
        receiverConfirmationCode = 5,
        receiverRegisterCode     = 6,
        receiverTxRegisteredCode = 7
    };

    struct WalletToNetworkBridge : public Bridge<INetworkIO> {
        BRIDGE_INIT(WalletToNetworkBridge);

        BRIDGE_FORWARD_IMPL(send_tx_invitation, wallet::sender::InvitationData::Ptr);
        BRIDGE_FORWARD_IMPL(send_tx_confirmation, wallet::sender::ConfirmationData::Ptr);
        BRIDGE_FORWARD_IMPL(send_output_confirmation, None);
        BRIDGE_FORWARD_IMPL(send_tx_confirmation, wallet::receiver::ConfirmationData::Ptr);
        BRIDGE_FORWARD_IMPL(register_tx, wallet::receiver::RegisterTxData::Ptr);
        BRIDGE_FORWARD_IMPL(send_tx_registered, UuidPtr);
    };

    struct NetworkToWalletBridge : public Bridge<IWallet> {
        BRIDGE_INIT(NetworkToWalletBridge);

        BRIDGE_FORWARD_IMPL(handle_tx_invitation, wallet::sender::InvitationData::Ptr);
        BRIDGE_FORWARD_IMPL(handle_tx_confirmation,wallet::sender::ConfirmationData::Ptr);
        BRIDGE_FORWARD_IMPL(handle_output_confirmation, None);
        BRIDGE_FORWARD_IMPL(handle_tx_confirmation, wallet::receiver::ConfirmationData::Ptr);
        BRIDGE_FORWARD_IMPL(handle_tx_registration, UuidPtr);
        BRIDGE_FORWARD_IMPL(handle_tx_failed, UuidPtr);
    };

    class WalletNetworkIO : public IMsgHandler
                          , public INetworkIO
    {
    public:
        using ConnectCallback = std::function<void(uint64_t tag, int status)>;
        WalletNetworkIO(io::Address address);
        void start();
        void stop();
        void wait();
        INetworkIO& get_network_proxy();
        void set_wallet_proxy(IWallet* wallet);
        void connect(io::Address address, ConnectCallback&& callback);
    private:
        // INetworkIO
        void send_tx_invitation(PeerId to, wallet::sender::InvitationData::Ptr&&) override;
        void send_tx_confirmation(PeerId to, wallet::sender::ConfirmationData::Ptr&&) override;
        void send_output_confirmation(PeerId to, None&&) override;
        void send_tx_confirmation(PeerId to, wallet::receiver::ConfirmationData::Ptr&&) override;
        void register_tx(PeerId to, wallet::receiver::RegisterTxData::Ptr&&) override;
        void send_tx_registered(PeerId to, UuidPtr&& txId) override;

        // IMsgHandler
        void on_protocol_error(uint64_t fromStream, ProtocolError error) override;;
        void on_connection_error(uint64_t fromStream, int errorCode) override;

        // handlers for the protocol messages
        bool on_sender_inviatation(uint64_t connectionId, wallet::sender::InvitationData&& data);

        void thread_func();
        void on_stream_accepted(io::TcpStream::Ptr&& newStream, int errorCode);
        void on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, int status);

        uint64_t get_connection_tag();
        
        template <typename T>
        void send(PeerId to, MsgType type, T&& data)
        {
            auto it = m_connections.find(to);
            if (it != m_connections.end()) {
                SerializedMsg msgToSend;
                m_protocol.serialize(msgToSend, type, data);

                it->second->write_msg(msgToSend); // TODO handle dead connection

                                                  // not needed any more (in this test)
            }
            else {
                LOG_ERROR() << "No connection";
                // add some handling
            }
        }

    private:
        Protocol<WalletNetworkIO> m_protocol;
        io::Address m_address;
        io::Reactor::Ptr m_reactor;
        io::TcpServer::Ptr m_server;
        WalletToNetworkBridge m_bridge;
        IWallet* m_wallet;
        Thread m_thread;
        std::map<uint64_t, std::unique_ptr<Connection>> m_connections;
        std::map<uint64_t, ConnectCallback> m_connections_callbacks;
        uint64_t m_connection_tag;
    };
}