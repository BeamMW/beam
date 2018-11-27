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
#include "utility/bridge.h"
#include "utility/logger.h"
#include "core/proto.h"
#include "utility/io/timer.h"
#include <boost/intrusive/set.hpp>
#include "wallet.h"

namespace beam
{
	class WalletNetworkViaBbs
		: public IWalletNetwork
	{
		IWallet& m_Wallet;
		proto::FlyClient::INetwork& m_NodeNetwork;
		IWalletDB::Ptr m_WalletDB;

		struct Addr
		{
			struct Wid :public boost::intrusive::set_base_hook<> {
				uint64_t m_OwnID;
				bool operator < (const Wid& x) const { return m_OwnID < x.m_OwnID; }
				IMPLEMENT_GET_PARENT_OBJ(Addr, m_Wid)
			} m_Wid;

			struct Channel :public boost::intrusive::set_base_hook<> {
				BbsChannel m_Value;
				bool operator < (const Channel& x) const { return m_Value < x.m_Value; }
				IMPLEMENT_GET_PARENT_OBJ(Addr, m_Channel)
			} m_Channel;

			ECC::Scalar::Native m_sk; // private addr
			PeerID m_Pk; // self public addr
		};

		typedef boost::intrusive::multiset<Addr::Wid> WidSet;
		WidSet m_Addresses;

		typedef  boost::intrusive::multiset<Addr::Channel> ChannelSet;
		ChannelSet m_Channels;

		void DeleteAddr(Addr&);
		bool IsSingleChannelUser(const Addr::Channel&);

		struct MyRequestBbsMsg
			:public proto::FlyClient::RequestBbsMsg
			,public boost::intrusive::list_base_hook<>
		{
			typedef boost::intrusive_ptr<MyRequestBbsMsg> Ptr;
			virtual ~MyRequestBbsMsg() {}
		};

		typedef boost::intrusive::list<MyRequestBbsMsg> BbsMsgList;
		BbsMsgList m_PendingBbsMsgs;

		void DeleteReq(MyRequestBbsMsg& r);

		struct BbsSentEvt
			:public proto::FlyClient::Request::IHandler
			,public proto::FlyClient::IBbsReceiver
		{
			virtual void OnComplete(proto::FlyClient::Request&) override;
			virtual void OnMsg(proto::BbsMsg&&) override;
			IMPLEMENT_GET_PARENT_OBJ(WalletNetworkViaBbs, m_BbsSentEvt)
		} m_BbsSentEvt;

		void OnMsg(const proto::BbsMsg&);

		static BbsChannel channel_from_wallet_id(const WalletID& walletID);

		std::map<BbsChannel, Timestamp> m_BbsTimestamps;
		io::Timer::Ptr m_pTimerBbsTmSave;
		void OnTimerBbsTmSave();
		void SaveBbsTimestamps();

	public:

		WalletNetworkViaBbs(IWallet&, proto::FlyClient::INetwork&, const IWalletDB::Ptr&);
		virtual ~WalletNetworkViaBbs();

		void new_own_address(uint64_t ownID, BbsChannel);
		void new_own_address(uint64_t ownID, const WalletID&);
		void address_deleted(uint64_t ownID);

		// IWalletNetwork
		virtual void Send(const WalletID& peerID, wallet::SetTxParameter&& msg) override;

	};
}
