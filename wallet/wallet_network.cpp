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

using namespace std;

namespace
{
    const char* BBS_TIMESTAMPS = "BbsTimestamps";
    const unsigned AddressUpdateInterval_ms = 60 * 1000; // check addresses every minute
}


namespace beam {

	WalletNetworkViaBbs::WalletNetworkViaBbs(IWallet& w, proto::FlyClient::INetwork& net, const IWalletDB::Ptr& pWalletDB)
		: m_Wallet(w)
		, m_NodeNetwork(net)
		, m_WalletDB(pWalletDB)
        , m_AddressExpirationTimer(io::Timer::create(io::Reactor::get_Current()))
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
			if (!address.isExpired())
				AddOwnAddress(address);

        m_AddressExpirationTimer->start(AddressUpdateInterval_ms, false, [this] { OnAddressTimer(); });
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

	void WalletNetworkViaBbs::DeleteAddr(const Addr& v)
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
		BbsChannel ret;
		walletID.m_Channel.Export(ret);
		return ret;
	}

	void WalletNetworkViaBbs::AddOwnAddress(const WalletAddress& address)
	{
        AddOwnAddress(address.m_OwnID, channel_from_wallet_id(address.m_walletID), address.getExpirationTime(), address.m_walletID);
	}

	void WalletNetworkViaBbs::AddOwnAddress(uint64_t ownID, BbsChannel nChannel, Timestamp expirationTime, const WalletID& walletID)
	{
		Addr::Wid key;
		key.m_OwnID = ownID;

		auto itW = m_Addresses.find(key);
		if (m_Addresses.end() != itW)
			return;

		Addr* pAddr = new Addr;
        pAddr->m_ExpirationTime = expirationTime;
		pAddr->m_Wid.m_OwnID = ownID;
		m_WalletDB->get_MasterKdf()->DeriveKey(pAddr->m_sk, Key::ID(ownID, Key::Type::Bbs));

		proto::Sk2Pk(pAddr->m_Pk, pAddr->m_sk); // needed to "normalize" the sk, and calculate the channel

		pAddr->m_Channel.m_Value = nChannel;

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

        LOG_INFO() << "WalletID " << to_string(walletID) << " subscribes to BBS channel " << pAddr->m_Channel.m_Value;
	}

	void WalletNetworkViaBbs::DeleteOwnAddress(uint64_t ownID)
	{
		Addr::Wid key;
		key.m_OwnID = ownID;

		auto it = m_Addresses.find(key);
		if (m_Addresses.end() != it)
			DeleteAddr(it->get_ParentObj());
	}

	void WalletNetworkViaBbs::OnTimerBbsTmSave()
	{
		m_pTimerBbsTmSave.reset();
		SaveBbsTimestamps();
	}

	void WalletNetworkViaBbs::BbsSentEvt::OnComplete(proto::FlyClient::Request& r)
	{
		assert(r.get_Type() == proto::FlyClient::Request::Type::BbsMsg);
		get_ParentObj().DeleteReq(static_cast<MyRequestBbsMsg&>(r));
	}

	void WalletNetworkViaBbs::BbsSentEvt::OnMsg(proto::BbsMsg&& msg)
	{
		get_ParentObj().OnMsg(msg);
	}

	void WalletNetworkViaBbs::OnMsg(const proto::BbsMsg& msg)
	{
		if (msg.m_Message.empty())
			return;

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

			ByteBuffer buf = msg.m_Message; // duplicate

			uint8_t* pMsg = &buf.front();
			uint32_t nSize = static_cast<uint32_t>(buf.size());

			if (!proto::Bbs::Decrypt(pMsg, nSize, it->get_ParentObj().m_sk))
				continue;

			wallet::SetTxParameter msgWallet;
			bool bValid = false;

			try {
				Deserializer der;
				der.reset(pMsg, nSize);
				der & msgWallet;
				bValid = true;
			}  catch (const std::exception&) {
				LOG_WARNING() << "BBS deserialization failed";
			}
				
			if (bValid)
			{
				WalletID wid;
				wid.m_Pk = it->get_ParentObj().m_Pk;
				wid.m_Channel = it->m_Value;
				m_Wallet.OnWalletMessage(wid, std::move(msgWallet));
				break;
			}
		}
	}

	void WalletNetworkViaBbs::Send(const WalletID& peerID, wallet::SetTxParameter&& msg)
	{
		Serializer ser;
		ser & msg;
		SerializeBuffer sb = ser.buffer();

		MyRequestBbsMsg::Ptr pReq(new MyRequestBbsMsg);

		ECC::NoLeak<ECC::Hash::Value> hvRandom;
		ECC::GenRandom(hvRandom.V);

		ECC::Scalar::Native nonce;
		m_WalletDB->get_MasterKdf()->DeriveKey(nonce, hvRandom.V);
		
		if (proto::Bbs::Encrypt(pReq->m_Msg.m_Message, peerID.m_Pk, nonce, sb.first, static_cast<uint32_t>(sb.second)))
		{
			pReq->m_Msg.m_Channel = channel_from_wallet_id(peerID);
			pReq->m_Msg.m_TimePosted = getTimestamp();

			m_PendingBbsMsgs.push_back(*pReq);
			pReq->AddRef();

			m_NodeNetwork.PostRequest(*pReq, m_BbsSentEvt);
		}
		else
		{
			LOG_WARNING() << "BBS serialization failed (bad peerID?)";
		}
	}

    void WalletNetworkViaBbs::OnAddressTimer()
    {
        vector<Addr*> addressesToDelete;
        for (const auto& address : m_Addresses)
        {
            if (address.get_ParentObj().IsExpired())
            {
                addressesToDelete.push_back(&address.get_ParentObj());
            }
        }
        for (const auto& address : addressesToDelete)
        {
            DeleteAddr(*address);
        }
        m_AddressExpirationTimer->start(AddressUpdateInterval_ms, false, [this] { OnAddressTimer(); });
    }
}
