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

#include "wallet_network.h"

// protocol version
#define WALLET_MAJOR 0
#define WALLET_MINOR 0
#define WALLET_REV   1

using namespace std;

namespace beam {

    WalletNetworkIO::WalletNetworkIO(io::Address node_address
                                   , IKeyChain::Ptr keychain
                                   , io::Reactor::Ptr reactor
                                   , unsigned reconnect_ms
                                   , unsigned sync_period_ms
                                   , uint64_t start_tag)
        : m_protocol{ WALLET_MAJOR, WALLET_MINOR, WALLET_REV, 150, *this, 20000 }
        , m_msgReader{ m_protocol, 1, 20000 }
        , m_node_address{node_address}
        , m_reactor{ !reactor ? io::Reactor::create() : reactor }
        , m_wallet{ nullptr }
        , m_keychain{keychain}
        , m_is_node_connected{false}
        , m_reactor_scope{*m_reactor }
        , m_reconnect_ms{ reconnect_ms }
        , m_sync_period_ms{ sync_period_ms }
        , m_sync_timer{io::Timer::create(m_reactor)}
        , m_last_bbs_message_time(0)
        , m_bbs_channel(0)
    {
        m_protocol.add_message_handler<WalletNetworkIO, wallet::Invite,             &WalletNetworkIO::on_message>(senderInvitationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::ConfirmTransaction, &WalletNetworkIO::on_message>(senderConfirmationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::ConfirmInvitation,  &WalletNetworkIO::on_message>(receiverConfirmationCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::TxRegistered,       &WalletNetworkIO::on_message>(receiverRegisteredCode, this, 1, 20000);
        m_protocol.add_message_handler<WalletNetworkIO, wallet::TxFailed,           &WalletNetworkIO::on_message>(failedCode, this, 1, 20000);
    }

    WalletNetworkIO::~WalletNetworkIO()
    {
        //assert(m_connections.empty());
        //assert(m_connectionWalletsIndex.empty());
    }

    void WalletNetworkIO::start()
    {
        m_reactor->run();
    }

    void WalletNetworkIO::stop()
    {
        m_reactor->stop();
    }

    void WalletNetworkIO::add_wallet(const WalletID& walletID)
    {
        m_wallets.insert(walletID);
    }

    void WalletNetworkIO::add_key_pair(const util::PubKey& pubKey, const util::PrivKey& privKey)
    {
        m_bbs_keys.push_back(make_pair(pubKey, privKey));
        LOG_INFO() << "Pubkey: " << to_string(pubKey);
        listen_to_bbs_channel(util::channel_from_wallet_id(pubKey));
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

    void WalletNetworkIO::close_node_connection()
    {
        LOG_DEBUG() << "Close node connection";
        m_is_node_connected = false;
        m_node_connection.reset();
        start_sync_timer();
    }

    bool WalletNetworkIO::on_message(uint64_t, wallet::Invite&& msg)
    {
        get_wallet().handle_tx_message(m_bbs_keys[0].first, move(msg));
        return true;
    }

    bool WalletNetworkIO::on_message(uint64_t, wallet::ConfirmTransaction&& msg)
    {
        get_wallet().handle_tx_message(move(msg));
        return true;
    }

    bool WalletNetworkIO::on_message(uint64_t, wallet::ConfirmInvitation&& msg)
    {
        get_wallet().handle_tx_message(move(msg));
        return true;
    }

    bool WalletNetworkIO::on_message(uint64_t, wallet::TxRegistered&& msg)
    {
        get_wallet().handle_tx_message(move(msg));
        return true;
    }

    bool WalletNetworkIO::on_message(uint64_t, wallet::TxFailed&& msg)
    {
        get_wallet().handle_tx_message(move(msg));
        return true;
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
        for (auto c : m_bbs_channels)
        {
            listen_to_bbs_channel(c);
        }

        vector<ConnectCallback> t;
        t.swap(m_node_connect_callbacks);
        for (auto& cb : t)
        {
            cb();
        }
    }

    void WalletNetworkIO::on_protocol_error(uint64_t, ProtocolError error)
    {
        LOG_ERROR() << "Wallet protocol error: " << error;
        m_msgReader.reset();

        //get_wallet().handle_connection_error(from);
//         if (m_connections.empty())
//         {
//             stop();
//             return;
//         }
    }

    void WalletNetworkIO::on_connection_error(uint64_t, io::ErrorCode errorCode)
    {
        LOG_ERROR() << "Wallet connection error: " << io::error_str(errorCode);
        m_msgReader.reset();
//         if (m_connections.empty())
//         {
//             stop();
//             return;
//         }
        //get_wallet().handle_connection_error(from);
    }

    void WalletNetworkIO::create_node_connection()
    {
        assert(!m_node_connection && !m_is_node_connected);
        m_node_connection = make_unique<WalletNodeConnection>(m_node_address, get_wallet(), m_reactor, m_reconnect_ms, *this);
    }

    /*
    void WalletNetworkIO::test_io_result(const io::Result res)
    {
        if (!res)
        {
            throw runtime_error(io::error_descr(res.error()));
        }
    }
    */

    void WalletNetworkIO::update_wallets(const WalletID& walletID)
    {
        auto p = m_keychain->getPeer(walletID);
        if (p.is_initialized())
        {
            add_wallet(p->m_walletID);
        }
    }

    bool WalletNetworkIO::handle_decrypted_message(uint64_t timestamp, const void* buf, size_t size)
    {
        m_last_bbs_message_time = timestamp;
        m_msgReader.new_data_from_stream(io::EC_OK, buf, size);
        return true;
    }

    void WalletNetworkIO::listen_to_bbs_channel(uint32_t channel)
    {
        m_bbs_channels.insert(channel);
        if (m_is_node_connected)
        {
            LOG_DEBUG() << "Listen BBS channel=" << channel;
            proto::BbsSubscribe msg;
            msg.m_Channel = channel;
            msg.m_TimeFrom = m_last_bbs_message_time;
            msg.m_On = true;
            send_to_node(move(msg));
        }
    }

    bool WalletNetworkIO::handle_bbs_message(proto::BbsMsg&& msg)
    {
        for (const auto& p : m_bbs_keys)
        {
            uint32_t channel = util::channel_from_wallet_id(p.first);

            LOG_DEBUG() << "BBS message form channel=" << msg.m_Channel << ". Listen channel=" << channel << " pubkey=" << to_string(p.first);

            uint8_t* out = 0;
            uint32_t size = 0;

            if (msg.m_Channel == channel)
            {
                if (util::decrypt(out, size, msg.m_Message, p.second))
                {
                    LOG_DEBUG() << "Succedded to decrypt BBS message form channel=" << msg.m_Channel;
                    return handle_decrypted_message(msg.m_TimePosted, out, size);
                }
                else
                {
                    LOG_DEBUG() << "failed to decrypt BBS message form channel=" << msg.m_Channel;
                }
            }
        }
        return true;
    }

    WalletNetworkIO::WalletNodeConnection::WalletNodeConnection(const io::Address& address, IWallet& wallet, io::Reactor::Ptr reactor, unsigned reconnectMsec, WalletNetworkIO& io)
        : m_address{address}
        , m_wallet {wallet}
        , m_connecting{false}
        , m_timer{io::Timer::create(reactor)}
        , m_reconnectMsec{reconnectMsec}
        , m_io{io}
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

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::BbsMsg&& msg)
    {
        return m_io.handle_bbs_message(move(msg));
    }
}
