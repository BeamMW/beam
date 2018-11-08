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
#define WALLET_REV   2

using namespace std;

namespace
{
    const char* BBS_TIMESTAMPS = "BbsTimestamps";
}


namespace beam {

	WalletNetworkViaBbs::WalletNetworkViaBbs(IWallet& w, proto::FlyClient::INetwork& net, const IKeyStore::Ptr& pKeyStore, const IWalletDB::Ptr& pWalletDB)
		:m_Wallet(w)
		,m_NodeNetwork(net)
		,m_pKeyStore(pKeyStore)
		,m_WalletDB(pWalletDB)
	{
		ByteBuffer buffer;
		m_WalletDB->getBlob(BBS_TIMESTAMPS, buffer);
		if (!buffer.empty())
		{
			Deserializer d;
			d.reset(buffer.data(), buffer.size());

			d & m_BbsTimestamps;
		}

		auto myAddresses = m_WalletDB->getAddresses(true);
		for (const auto& address : myAddresses)
			if (address.isExpired())
				m_pKeyStore->disable_key(address.m_walletID);

		std::set<PubKey> pubKeys;
		m_pKeyStore->get_enabled_keys(pubKeys);
		for (const auto& v : pubKeys)
			new_own_address(v);

	}

	WalletNetworkViaBbs::~WalletNetworkViaBbs()
	{
		while (!m_PendingBbsMsgs.empty())
			DeleteReq(m_PendingBbsMsgs.front());

		while (!m_Addresses.empty())
			DeleteAddr(m_Addresses.begin()->get_ParentObj());

		try {
			SaveBbsTimestamps();
		} catch (...) {
		}
	}

	void WalletNetworkViaBbs::SaveBbsTimestamps()
	{
		Timestamp tsThreshold = getTimestamp() - 3600 * 24 * 3;

		for (auto it = m_BbsTimestamps.begin(); m_BbsTimestamps.end() != it; )
		{
			auto it2 = it++;
			if (it2->second < tsThreshold)
				m_BbsTimestamps.erase(it2);
		}

		Serializer s;
		s & m_BbsTimestamps;

		ByteBuffer buffer;
		s.swap_buf(buffer);

		m_WalletDB->setVarRaw(BBS_TIMESTAMPS, buffer.data(), static_cast<int>(buffer.size()));
	}

	void WalletNetworkViaBbs::DeleteAddr(Addr& v)
	{
		if (IsSingleChannelUser(v.m_Channel))
			m_NodeNetwork.BbsSubscribe(v.m_Channel.m_Value, 0, NULL);

		m_Addresses.erase(WidSet::s_iterator_to(v.m_Wid));
		m_Channels.erase(ChannelSet::s_iterator_to(v.m_Channel));
		delete &v;
	}

	bool WalletNetworkViaBbs::IsSingleChannelUser(const Addr::Channel& c)
	{
		ChannelSet::const_iterator it = ChannelSet::s_iterator_to(c);
		ChannelSet::const_iterator it2 = it;
		if (((++it2) != m_Channels.end()) && (it2->m_Value == c.m_Value))
			return false;

		if (it != m_Channels.begin())
		{
			it2 = it;
			if ((--it2)->m_Value == it->m_Value)
				return false;
		}

		return true;
	}

	void WalletNetworkViaBbs::DeleteReq(MyRequestBbsMsg& r)
	{
		m_PendingBbsMsgs.erase(BbsMsgList::s_iterator_to(r));
		r.m_pTrg = NULL;
		r.Release();
	}

	BbsChannel WalletNetworkViaBbs::channel_from_wallet_id(const WalletID& walletID)
	{
		// TODO to be reviewed, 32 channels
		return walletID.m_pData[0] >> 3;
	}

	void WalletNetworkViaBbs::new_own_address(const WalletID& wid)
	{
		Addr::Wid key;
		key.m_Value = wid;

		auto itW = m_Addresses.find(key);
		if (m_Addresses.end() != itW)
			return;

		Addr* pAddr = new Addr;
		pAddr->m_Wid.m_Value = wid;
		pAddr->m_Channel.m_Value = channel_from_wallet_id(wid);

		m_Addresses.insert(pAddr->m_Wid);
		m_Channels.insert(pAddr->m_Channel);

		if (IsSingleChannelUser(pAddr->m_Channel))
		{
			Timestamp ts = 0;
			auto it = m_BbsTimestamps.find(pAddr->m_Channel.m_Value);
			if (m_BbsTimestamps.end() != it)
				ts = it->second;

			m_NodeNetwork.BbsSubscribe(pAddr->m_Channel.m_Value, ts, &m_BbsSentEvt);
		}
	}

	void WalletNetworkViaBbs::address_deleted(const WalletID& wid)
	{
		Addr::Wid key;
		key.m_Value = wid;

		auto it = m_Addresses.find(key);
		if (m_Addresses.end() != it)
			DeleteAddr(it->get_ParentObj());
	}

	void WalletNetworkViaBbs::OnTimerBbsTmSave()
	{
		m_pTimerBbsTmSave.reset();
		SaveBbsTimestamps();
	}

	void WalletNetworkViaBbs::BbsSentEvt::OnRequestComplete(Request& r)
	{
		assert(r.get_Type() == Request::Type::BbsMsg);
		get_ParentObj().DeleteReq(static_cast<MyRequestBbsMsg&>(r));
	}

	void WalletNetworkViaBbs::BbsSentEvt::OnMsg(proto::BbsMsg&& msg)
	{
		get_ParentObj().OnMsg(msg);
	}

	void WalletNetworkViaBbs::OnMsg(const proto::BbsMsg& msg)
	{
		auto itBbs = m_BbsTimestamps.find(msg.m_Channel);
		if (m_BbsTimestamps.end() != itBbs)
			itBbs->second = std::max(itBbs->second, msg.m_TimePosted);
		else
			m_BbsTimestamps[msg.m_Channel] = msg.m_TimePosted;

		if (!m_pTimerBbsTmSave)
		{
			m_pTimerBbsTmSave = io::Timer::create(io::Reactor::get_Current());
			m_pTimerBbsTmSave->start(60*1000, false, [this]() { OnTimerBbsTmSave(); });
		}

		Addr::Channel key;
		key.m_Value = msg.m_Channel;

		for (ChannelSet::iterator it = m_Channels.lower_bound(key); ; it++)
		{
			if (m_Channels.end() == it)
				break;
			if (it->m_Value != msg.m_Channel)
				break; // as well

			Blob blob;
			ByteBuffer buf = msg.m_Message; // duplicate
			if (m_pKeyStore->decrypt((uint8_t*&) blob.p, blob.n, buf, it->get_ParentObj().m_Wid.m_Value))
			{
				wallet::SetTxParameter msgWallet;
				bool bValid = false;

				try {
					Deserializer der;
					der.reset(blob.p, blob.n);
					der & msgWallet;
					bValid = true;
				}  catch (const std::exception&) {
					LOG_WARNING() << "BBS deserialization failed";
				}
				
				if (bValid)
				{
					m_Wallet.OnWalletMsg(it->get_ParentObj().m_Wid.m_Value, std::move(msgWallet));
					break;
				}
			}
		}
	}

	void WalletNetworkViaBbs::Send(const WalletID& peerID, wallet::SetTxParameter&& msg)
	{
		Serializer ser;
		ser & msg;
		SerializeBuffer sb = ser.buffer();

		MyRequestBbsMsg::Ptr pReq(new MyRequestBbsMsg);

//		ECC::NoLeak<ECC::Scalar> s_;
//		ECC::GenRandom(s_.V.m_Value.m_pData, s_.V.m_Value.nBytes);
//		ECC::Scalar::Native nonce(s_);
//		if (proto::BbsEncrypt(pReq->m_Msg.m_Message, peerID, nonce, sb.first, (uint32_t) sb.second))
		if (m_pKeyStore->encrypt(pReq->m_Msg.m_Message, sb.first, sb.second, peerID))
		{
			pReq->m_Msg.m_Channel = channel_from_wallet_id(peerID);
			pReq->m_Msg.m_TimePosted = getTimestamp();

			MyRequestBbsMsg* pR = pReq.detach();
			m_PendingBbsMsgs.push_back(*pR);

			pR->m_pTrg = &m_BbsSentEvt;
			m_NodeNetwork.PostRequest(*pR);
		}
	}

/*
    WalletNetworkIO::WalletNetworkIO(io::Address node_address
                                   , IWalletDB::Ptr walletDB
                                   , IKeyStore::Ptr keyStore
                                   , io::Reactor::Ptr reactor
                                   , unsigned reconnect_ms
                                   , unsigned sync_period_ms)

        : m_protocol{ WALLET_MAJOR, WALLET_MINOR, WALLET_REV, 150, *this, 20000 }
        , m_msgReader{ m_protocol, 1, 20000 }
        , m_walletID(Zero)
        , m_node_address{node_address}
        , m_reactor{ !reactor ? io::Reactor::create() : reactor }
        , m_WalletDB{walletDB}
        , m_is_node_connected{false}
        , m_reactor_scope{*m_reactor }
        , m_reconnect_ms{ reconnect_ms }
        , m_sync_period_ms{ sync_period_ms }
        , m_close_timeout_ms{ 10 *1000 }
        , m_sync_timer{io::Timer::create(*m_reactor)}
        , m_keystore(keyStore)
        , m_lastReceiver(0)
    {
        m_protocol.add_message_handler<WalletNetworkIO, wallet::SetTxParameter,     &WalletNetworkIO::on_message>(setTxParameterCode, this, 1, 20000);

        ByteBuffer buffer;
        m_WalletDB->getBlob(BBS_TIMESTAMPS, buffer);
        if (!buffer.empty())
        {
            Deserializer d;
            d.reset(buffer.data(), buffer.size());

            d & m_bbs_timestamps;
        }

        auto myAddresses = m_WalletDB->getAddresses(true);
        for (const auto& address : myAddresses)
        {
            if (address.isExpired())
            {
                m_keystore->disable_key(address.m_walletID);
            }
        }

        m_keystore->get_enabled_keys(m_myPubKeys);
        for (const auto& k : m_myPubKeys)
        {
            listen_to_bbs_channel(k);
        }
    }

    WalletNetworkIO::~WalletNetworkIO()
    {
        try
        {
            Serializer s;
            s & m_bbs_timestamps;
            ByteBuffer buffer;
            s.swap_buf(buffer);
            if (!buffer.empty())
            {
                m_WalletDB->setVarRaw(BBS_TIMESTAMPS, buffer.data(), static_cast<int>(buffer.size()));
            }
        }
        catch(...)
        { }
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

    void WalletNetworkIO::send_tx_message(const WalletID& to, wallet::SetTxParameter&& msg)
    {
        send(to, setTxParameterCode, move(msg));
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

    void WalletNetworkIO::send_node_message(proto::Recover&& msg)
    {
        send_to_node(move(msg));
    }

    void WalletNetworkIO::send_node_message(proto::GetProofState&& msg)
    {
        send_to_node(move(msg));
    }

    void WalletNetworkIO::send_node_message(proto::GetProofKernel&& msg)
    {
        send_to_node(move(msg));
    }

    void WalletNetworkIO::new_own_address(const WalletID& address)
    {
        auto p = m_myPubKeys.insert(address);
        if (p.second)
        {
            listen_to_bbs_channel(address);
        }
    }

    void WalletNetworkIO::address_deleted(const WalletID& address)
    {
        m_myPubKeys.erase(address);
    }

    void WalletNetworkIO::close_node_connection()
    {
        if (m_is_node_connected && !m_close_timer)
        {
            m_close_timer = io::Timer::create(*m_reactor);
            m_close_timer->start(m_close_timeout_ms, false, BIND_THIS_MEMFN(on_close_connection_timer));
        }
    }

    void WalletNetworkIO::on_close_connection_timer()
    {
        LOG_DEBUG() << "Close node connection";

        reset_connection();
        start_sync_timer();
    }

    void WalletNetworkIO::postpone_close_timer()
    {
        if (m_close_timer)
        {
            m_close_timer->restart(m_close_timeout_ms, false);
        }
    }

    bool WalletNetworkIO::on_message(uint64_t, wallet::SetTxParameter&& msg)
    {
        assert(m_lastReceiver);
        get_wallet().handle_tx_message(*m_lastReceiver, move(msg));
        return true;
    }

    void WalletNetworkIO::connect_node()
    {
        if (m_is_node_connected == false && !m_node_connection && !m_node_address.empty())
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
        for (const auto& k : m_myPubKeys)
        {
            listen_to_bbs_channel(k);
        }

        vector<ConnectCallback> t;
        t.swap(m_node_connect_callbacks);
        for (auto& cb : t)
        {
            cb();
        }
    }

    void WalletNetworkIO::on_node_disconnected()
    {
        m_is_node_connected = false;
    }

    void WalletNetworkIO::on_protocol_error(uint64_t, ProtocolError error)
    {
        LOG_ERROR() << "Wallet protocol error: " << error;
        m_msgReader.reset();
    }

    void WalletNetworkIO::on_connection_error(uint64_t, io::ErrorCode errorCode)
    {
        LOG_ERROR() << "Wallet connection error: " << io::error_str(errorCode);
        m_msgReader.reset();
    }

    void WalletNetworkIO::create_node_connection()
    {
        assert(!m_node_connection && !m_is_node_connected);
        m_node_connection = make_unique<WalletNodeConnection>(m_node_address, get_wallet(), m_reactor, m_reconnect_ms, *this);
    }

    void WalletNetworkIO::update_wallets(const WalletID& walletID)
    {
        auto p = m_WalletDB->getPeer(walletID);
        if (p.is_initialized())
        {
            add_wallet(p->m_walletID);
        }
    }

    bool WalletNetworkIO::handle_decrypted_message(uint64_t timestamp, const void* buf, size_t size)
    {
        assert(m_lastReceiver);
        m_bbs_timestamps[channel_from_wallet_id(*m_lastReceiver)] = timestamp;
        m_msgReader.new_data_from_stream(io::EC_OK, buf, size);
        return true;
    }

    void WalletNetworkIO::listen_to_bbs_channel(const WalletID& walletID)
    {
        if (m_is_node_connected)
        {
            uint32_t channel = channel_from_wallet_id(walletID);
            LOG_INFO() << "WalletID " << to_string(walletID) << " subscribes to BBS channel " << channel;
            proto::BbsSubscribe msg;
            msg.m_Channel = channel;
            msg.m_TimeFrom = m_bbs_timestamps[channel];
            msg.m_On = true;
            send_to_node(move(msg));
        }
    }

    bool WalletNetworkIO::handle_bbs_message(proto::BbsMsg&& msg)
    {
        postpone_close_timer();
        uint8_t* out = 0;
        uint32_t size = 0;

        for (const auto& k : m_myPubKeys)
        {
            uint32_t channel = channel_from_wallet_id(k);

            if (channel != msg.m_Channel) continue;
            ByteBuffer decryptBuffer(msg.m_Message); // we have to allocate memory for decrypted message
            if (m_keystore->decrypt(out, size, decryptBuffer, k))
            {
                LOG_DEBUG() << "Succeeded to decrypt BBS message from channel=" << msg.m_Channel;
                m_lastReceiver = &k;
                return handle_decrypted_message(msg.m_TimePosted, out, size);
            }
        }

        return true;
    }

    void WalletNetworkIO::set_node_address(io::Address node_address)
    {
        reset_connection();

        m_node_address = node_address;

        connect_node();
    }

    void WalletNetworkIO::reset_connection()
    {
        get_wallet().abort_sync();

        m_close_timer.reset();
        m_is_node_connected = false;
        m_node_connection.reset();
    }

    WalletNetworkIO::WalletNodeConnection::WalletNodeConnection(const io::Address& address, IWallet& wallet, io::Reactor::Ptr reactor, unsigned reconnectMsec, WalletNetworkIO& io)
        : m_address{address}
        , m_wallet {wallet}
        , m_connecting{false}
        , m_timer{io::Timer::create(*reactor)}
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

    void WalletNetworkIO::WalletNodeConnection::OnConnectedSecure()
    {
        LOG_INFO() << "Wallet connected to node";
        m_connecting = false;
        proto::Config msgCfg;
        msgCfg.m_CfgChecksum = Rules::get().Checksum;
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
        m_io.on_node_disconnected();
        m_wallet.abort_sync();
        m_timer->cancel();
        Reset();
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

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::ProofKernel&& msg)
    {
        return m_wallet.handle_node_message(move(msg));
    }

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::NewTip&& msg)
    {
        return m_wallet.handle_node_message(move(msg));
    }

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::Mined&& msg)
    {
        return m_wallet.handle_node_message(move(msg));
    }

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::Recovered&& msg)
    {
        return m_wallet.handle_node_message(move(msg));
    }

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::ProofState&& msg)
    {
        return m_wallet.handle_node_message(move(msg));
    }

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::BbsMsg&& msg)
    {
        return m_io.handle_bbs_message(move(msg));
    }

    bool WalletNetworkIO::WalletNodeConnection::OnMsg2(proto::Authentication&& msg)
    {
        proto::NodeConnection::OnMsg(std::move(msg));

        if (proto::IDType::Node == msg.m_IDType)
        {
            ECC::Scalar::Native sk;
            if (m_wallet.get_IdentityKeyForNode(sk, msg.m_ID))
            {
                ProveID(sk, proto::IDType::Owner);
            }
        }

        return true;
    }
*/

}
