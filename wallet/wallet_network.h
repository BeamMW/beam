#pragma once

#include "p2p/protocol.h"
#include "p2p/connection.h"
#include "p2p/msg_reader.h"
#include "bbsutil.h"
#include "utility/bridge.h"
#include "utility/logger.h"
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


        WalletNetworkIO(io::Address node_address
                      , IKeyChain::Ptr keychain
                      , io::Reactor::Ptr reactor = io::Reactor::Ptr()
                      , unsigned reconnect_ms = 1000 // 1 sec
                      , unsigned sync_period_ms = 60 * 1000  // 1 minute
                      , uint64_t start_tag = 0);
        virtual ~WalletNetworkIO();

        void start();
        void stop();

        void add_wallet(const WalletID& walletID);

        // TODO now from add_wallet
        void listen_to_bbs_channel(uint32_t channel);

    private:
        // INetworkIO
        void send_tx_message(wallet::Invite&&) override;
        void send_tx_message(wallet::ConfirmTransaction&&) override;
        void send_tx_message(wallet::ConfirmInvitation&&) override;
        void send_tx_message(wallet::TxRegistered&&) override;
        void send_tx_message(wallet::TxFailed&&) override;

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

        bool handle_decrypted_message(const void* buf, size_t size) override;

        // handlers for the protocol messages
        bool on_message(uint64_t, wallet::Invite&& msg);
        bool on_message(uint64_t, wallet::ConfirmTransaction&& msg);
        bool on_message(uint64_t, wallet::ConfirmInvitation&& msg);
        bool on_message(uint64_t, wallet::TxRegistered&& msg);
        bool on_message(uint64_t, wallet::TxFailed&& msg);

        void start_sync_timer();
        void on_sync_timer();
        void on_node_connected();

        uint64_t get_connection_tag();
        void create_node_connection();
        void add_connection(uint64_t tag, ConnectionInfo&&);

        template <typename T>
        void send(const WalletID& walletID, MsgType type, T&& msg)
        {
            update_wallets(walletID);

            // send BBS message
            proto::BbsMsg bbsMsg;
            bbsMsg.m_Channel = util::channel_from_wallet_id(walletID);
            bbsMsg.m_TimePosted = local_timestamp_msec();
            m_protocol.serialize(m_msgToSend, type, msg);
            util::encrypt(bbsMsg.m_Message, m_msgToSend, walletID);
            send_to_node(std::move(bbsMsg));
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

        void update_wallets(const WalletID& walletID);

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
            bool OnMsg2(proto::BbsMsg&& msg) override;
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
        MsgReader m_msgReader;
        WalletID m_walletID;
        io::Address m_node_address;
        io::Reactor::Ptr m_reactor;
        IWallet* m_wallet;
        IKeyChain::Ptr m_keychain;

        std::set<WalletID> m_wallets;

        bool m_is_node_connected;
        io::Reactor::Scope m_reactor_scope;
        unsigned m_reconnect_ms;
        unsigned m_sync_period_ms;
        std::unique_ptr<WalletNodeConnection> m_node_connection;
        SerializedMsg m_msgToSend;
        io::Timer::Ptr m_sync_timer;
        uint64_t m_last_bbs_message_time;
    };
}
