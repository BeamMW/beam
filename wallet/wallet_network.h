// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "p2p/protocol.h"
#include "p2p/connection.h"
#include "p2p/msg_reader.h"
#include "keystore.h"
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

    inline uint32_t channel_from_wallet_id(const WalletID& walletID) {
        // TODO to be reviewed, 32 channels
        return walletID.m_pData[0] >> 3;
    }

    class WalletNetworkIO : public IErrorHandler
                          , public NetworkIOBase
    {
        using ConnectCallback = std::function<void()>;
    public:


        WalletNetworkIO(io::Address node_address
                      , IKeyChain::Ptr keychain
                      , IKeyStore::Ptr keyStore
                      , io::Reactor::Ptr reactor = io::Reactor::Ptr()
                      , unsigned reconnect_ms = 1000 // 1 sec
                      , unsigned sync_period_ms = 20 * 1000);  // 20 sec

        virtual ~WalletNetworkIO();

        void start();
        void stop();

        void add_wallet(const WalletID& walletID);

        // TODO now from add_wallet ???
        void listen_to_bbs_channel(uint32_t channel);

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

        //void close_connection(const WalletID& id) override;
        void connect_node() override;
        void close_node_connection() override;

        // IMsgHandler
        void on_protocol_error(uint64_t fromStream, ProtocolError error) override;;
        void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) override;

        bool handle_decrypted_message(uint64_t timestamp, const void* buf, size_t size);

        // handlers for the protocol messages
        bool on_message(uint64_t, wallet::Invite&& msg);
        bool on_message(uint64_t, wallet::ConfirmTransaction&& msg);
        bool on_message(uint64_t, wallet::ConfirmInvitation&& msg);
        bool on_message(uint64_t, wallet::TxRegistered&& msg);
        bool on_message(uint64_t, wallet::TxFailed&& msg);

        void start_sync_timer();
        void on_sync_timer();
        void on_close_connection_timer();
        void postpone_close_timer();
        void cancel_close_timer();
        void on_node_connected();
        void on_node_disconnected();

        void create_node_connection();

        template <typename T>
        void send(const WalletID& walletID, MsgType type, T&& msg, const WalletID* from=0)
        {
            update_wallets(walletID);

            msg.m_from = from ? *from : *m_myPubKeys.begin();

            uint32_t channel = channel_from_wallet_id(walletID);
            LOG_DEBUG() << "BBS send message to channel=" << channel << "[" << to_hex(walletID.m_pData, 32) << "]  my pubkey=" << to_hex(msg.m_from.m_pData, 32);
            proto::BbsMsg bbsMsg;
            bbsMsg.m_Channel = channel;
            bbsMsg.m_TimePosted = getTimestamp();
            m_protocol.serialize(m_msgToSend, type, msg);

            if (!m_keystore->encrypt(bbsMsg.m_Message, m_msgToSend, walletID)) {
                LOG_ERROR() << "Failed to encrypt BBS message";
            } else {
                send_to_node(std::move(bbsMsg));
            }
        }

        template<typename T>
        void send_to_node(T&& msg)
        {
            if (!m_is_node_connected)
            {
                auto f = [this, msg = std::move(msg)]()
                {
                    m_node_connection->Send(msg);
                };
                m_node_connect_callbacks.emplace_back(std::move(f));
                connect_node();
            }
            else
            {
                postpone_close_timer();
                m_node_connection->Send(msg);
            }
        }

        void update_wallets(const WalletID& walletID);

        bool handle_bbs_message(proto::BbsMsg&& msg);

        class WalletNodeConnection : public proto::NodeConnection
        {
        public:
            using NodeConnectCallback = std::function<void()>;
            WalletNodeConnection(const io::Address& address, IWallet& wallet, io::Reactor::Ptr reactor, unsigned reconnectMsec, WalletNetworkIO& io);
            void connect(NodeConnectCallback&& cb);
        private:
            // NodeConnection
            void OnConnectedSecure() override;
			void OnDisconnect(const DisconnectReason&) override;
			bool OnMsg2(proto::Boolean&& msg) override;
            bool OnMsg2(proto::ProofUtxo&& msg) override;
			bool OnMsg2(proto::ProofStateForDummies&& msg) override;
			bool OnMsg2(proto::NewTip&& msg) override;
            bool OnMsg2(proto::Hdr&& msg) override;
            bool OnMsg2(proto::Mined&& msg) override;
            bool OnMsg2(proto::BbsMsg&& msg) override;
        private:
            io::Address m_address;
            IWallet & m_wallet;
            std::vector<NodeConnectCallback> m_callbacks;
            bool m_connecting;
            io::Timer::Ptr m_timer;
            unsigned m_reconnectMsec;
            WalletNetworkIO& m_io;
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
        unsigned m_close_timeout_ms;
        std::unique_ptr<WalletNodeConnection> m_node_connection;
        SerializedMsg m_msgToSend;
        io::Timer::Ptr m_sync_timer;
        io::Timer::Ptr m_close_timer;

        std::vector<ConnectCallback> m_node_connect_callbacks;

        // channel# -> last message time
        std::map<uint32_t, uint64_t> m_bbs_timestamps;
        IKeyStore::Ptr m_keystore;
        std::set<PubKey> m_myPubKeys;
        const WalletID* m_lastReceiver;
    };
}
