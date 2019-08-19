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

#include "../node.h"
#include "../../core/fly_client.h"
#include "../../core/treasury.h"
#include "../../core/lightning.h"

#define LOG_VERBOSE_ENABLED 0
#include "utility/logger.h"

namespace beam {
namespace Lightning	{

#ifdef WIN32
const char* g_sz = "mytest.db";
#else // WIN32
const char* g_sz = "/tmp/mytest.db";
#endif // WIN32


void CreateTestKdf(Key::IKdf::Ptr& pKdf, size_t iWallet)
{
	ECC::Hash::Value hv;
	ECC::Hash::Processor()
		<< "test-wallet"
		<< iWallet
		>> hv;

	ECC::HKdf::Create(pKdf, hv);
}


struct Client
{
	struct WalletID
	{
		uintBigFor<BbsChannel>::Type m_Channel;
		PeerID m_Pk;

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_Channel
				& m_Pk;
		}
	};

	Key::IKdf::Ptr m_pKdf;

	Block::SystemState::HistoryMap m_Hdrs;
	Height get_TipHeight() const
	{
		return m_Hdrs.m_Map.empty() ? 0 : m_Hdrs.m_Map.rbegin()->first;
	}

	std::string m_sName;

	// my bbs address
	WalletID m_Wid;
	ECC::Scalar::Native m_skBbs;

	typedef std::map<Key::IDV, Height> CoinMap;
	CoinMap m_Coins;

	uint64_t m_nNextCoinID = 100500;

	typedef std::map<uint32_t, ByteBuffer> FieldMap;

	size_t m_OverrideWithdrawal = static_cast<size_t>(-1);

	struct Channel
		:public Lightning::Channel
	{
		struct Key
			:public boost::intrusive::set_base_hook<>
		{
			typedef uintBig_t<16> Type;
			Type m_ID;

			bool operator < (const Key& t) const { return (m_ID < t.m_ID); }

			IMPLEMENT_GET_PARENT_OBJ(Channel, m_Key)
		} m_Key;


		typedef boost::intrusive::multiset<Key> Map;

		State::Enum m_LastState = State::None;

		void LogNewState()
		{
			State::Enum eState = get_State();
			if (m_LastState == eState)
				return;

			m_LastState = eState;

			std::ostringstream os;
			os << m_This.m_sName << "State ";

			switch (eState)
			{
			case State::Opening0:
				os << "Opening0 (early stage, safe to discard)";
				break;
			case State::Opening1:
				os << "Opening1 (negotiating, no-return barrier crossed)";
				break;
			case State::Opening2:
				os << "Opening2 (Waiting channel open confirmation)";
				break;
			case State::OpenFailed:
				os << "OpenFailed (Not confirmed, missed height window). Waiting for " << m_SafeForgetHeight << " confirmations before forgetting";
				break;
			case State::Open:
				os << "Open. Last Revision: " << m_vUpdates.size() << ". Balance: " << m_vUpdates.back()->m_Outp.m_Value << " / " << (m_vUpdates.back()->m_msMy.m_Value - m_Params.m_Fee);
				break;
			case State::Updating:
				os << "Updating (creating newer Revision)";
				break;
			case State::Closing1:
				os << "Closing1 (decided to close, sent Phase-1 withdrawal)";
				break;
			case State::Closing2:
				{
					os << "Closing2 (Phase-1 withdrawal detected). Revision: " << m_State.m_Close.m_iPath << ". Initiated by " << (m_State.m_Close.m_Initiator ? "me" : "peer");
					if (DataUpdate::Type::Punishment == m_vUpdates[m_State.m_Close.m_iPath]->m_Type)
						os << ". Fraudulent withdrawal attempt detected! Will claim everything";
				}
				break;
			case State::Closed:
				os << "Closed. Waiting for " << m_SafeForgetHeight << " confirmations before forgetting";
				break;
			default:
				return;
			}


			std::cout << os.str() << std::endl;
		}

		Client& m_This;

		WalletID m_widTrg;

		Channel(Client& x) :m_This(x)
		{
			std::cout << m_This.m_sName << "+Channel" << std::endl;
		}

		~Channel()
		{
			std::cout << m_This.m_sName << "-Channel" << std::endl;
		}

		virtual Height get_Tip() const override { return m_This.get_TipHeight(); }
		virtual proto::FlyClient::INetwork& get_Net() override { return m_This.m_Conn; }
		virtual void get_Kdf(ECC::Key::IKdf::Ptr& pKdf) override { pKdf = m_This.m_pKdf; }

		virtual void AllocTxoID(ECC::Key::IDV& kidv) override
		{
			kidv.set_Subkey(0);
			kidv.m_Idx = m_This.m_nNextCoinID++;
		}

		virtual Amount SelectInputs(std::vector<ECC::Key::IDV>& vInp, Amount valRequired) override
		{
			assert(vInp.empty());
			Height h = m_This.get_TipHeight();
			Amount nDone = 0;

			for (CoinMap::iterator it = m_This.m_Coins.begin(); (nDone < valRequired) && (m_This.m_Coins.end() != it); it++)
			{
				if (it->second <= h)
				{
					nDone += it->first.m_Value;
					vInp.push_back(it->first);
				}
			}

			return nDone;

		}

		virtual size_t SelectWithdrawalPath() override
		{
			if (m_This.m_OverrideWithdrawal < m_vUpdates.size())
				return m_This.m_OverrideWithdrawal;

			return Lightning::Channel::SelectWithdrawalPath(); // default
		}

		struct Codes
		{
			static const uint32_t Control0 = 1024 << 16;
			static const uint32_t MyWid = Control0 + 31;
		};

		bool m_SendMyWid = true;
		Height m_SafeForgetHeight = 8;

		virtual void SendPeer(Storage::Map&& dataOut) override
		{
			assert(!dataOut.empty());

			if (m_SendMyWid)
			{
				m_SendMyWid = false;
				dataOut.Set(m_This.m_Wid, Codes::MyWid);
			}

			Serializer ser;
			ser & m_Key.m_ID;
			ser & Cast::Down<FieldMap>(dataOut);

			std::cout << m_This.m_sName << "\tTo peer (via bbs): " << ser.buffer().second << std::endl;

			m_This.Send(*this, ser);
		}

		virtual void OnCoin(const ECC::Key::IDV& kidv, Height h, CoinState eState, bool bReverse) override
		{
			const char* szStatus = "";

			switch (eState)
			{
			case CoinState::Locked:
				szStatus = bReverse ? "Unlocked" : "Locked";
				if (bReverse)
					m_This.m_Coins[kidv] = h;
				else
					m_This.m_Coins.erase(kidv);
				break;

			case CoinState::Spent:
				szStatus = bReverse ? "Unspent" : "Spent";
				if (bReverse)
					m_This.m_Coins[kidv] = h;
				else
					m_This.m_Coins.erase(kidv);
				break;

			case CoinState::Confirmed:
				szStatus = bReverse ? "Unconfirmed" : "Confirmed";
				if (bReverse)
					m_This.m_Coins.erase(kidv);
				else
					m_This.m_Coins[kidv] = h;
				break;


			default: // suppress warning
				break;
			}

			std::cout << m_This.m_sName << "Coin " << kidv.m_Value << " " << szStatus << std::endl;
		}

		void Close()
		{
			Lightning::Channel::Close();
			MaybeDelete();
		}

		void MaybeDelete()
		{
			if (!IsNegotiating() && IsSafeToForget(m_SafeForgetHeight))
			{
				Forget();
				m_This.DeleteChannel(*this);
			}
		}

	};

	Channel::Map m_Channels;

	Channel& AddChannel(const Channel::Key& key)
	{
		Channel* pCh = new Channel(*this);
		pCh->m_Key = key;
		m_Channels.insert(pCh->m_Key);
		return *pCh;
	}

	struct NodeEvts
		:public proto::FlyClient
		,public proto::FlyClient::Request::IHandler
		,public proto::FlyClient::IBbsReceiver
	{
		// proto::FlyClient
		virtual void OnNewTip() override { get_ParentObj().OnNewTip(); }
		virtual void OnRolledBack() override;
		virtual Block::SystemState::IHistory& get_History() override { return get_ParentObj().m_Hdrs; }
		virtual void OnOwnedNode(const PeerID&, bool bUp) override {}
		// proto::FlyClient::Request::IHandler
		virtual void OnComplete(Request&) override {}
		// proto::FlyClient::IBbsReceiver
		virtual void OnMsg(proto::BbsMsg&&) override;

		IMPLEMENT_GET_PARENT_OBJ(Client, m_NodeEvts)
	} m_NodeEvts;

	struct MyNetwork
		:public proto::FlyClient::NetworkStd
	{
		using proto::FlyClient::NetworkStd::NetworkStd;

		virtual void PostRequestInternal(proto::FlyClient::Request& r) override
		{
			if (proto::FlyClient::Request::Type::Transaction == r.get_Type())
				std::cout << get_ParentObj().m_sName << "### Broadcasting transaction ###" << std::endl;

			proto::FlyClient::NetworkStd::PostRequestInternal(r);
		}


		IMPLEMENT_GET_PARENT_OBJ(Client, m_Conn)
	} m_Conn;

	void OnMsg(Blob&&);

	void Send(Channel& c, Serializer&);

	void DeleteChannel(Channel& c)
	{
		m_Channels.erase(Channel::Map::s_iterator_to(c.m_Key));
		delete &c;
	}

	Client()
		:m_Conn(m_NodeEvts)
	{
	}

	~Client()
	{
		while (!m_Channels.empty())
			DeleteChannel(m_Channels.begin()->get_ParentObj());
	}

	void OnNewTip();

	void Initialize();

	bool OpenChannel(const WalletID& widTrg, Amount nMy, Amount nTrg);
};


void Client::OnNewTip()
{
	for (Channel::Map::iterator it = m_Channels.begin(); m_Channels.end() != it; )
	{
		Channel& c = (it++)->get_ParentObj();
		c.Update();
		c.LogNewState();
		c.MaybeDelete();
	}
}

void Client::NodeEvts::OnRolledBack()
{
	for (Channel::Map::iterator it = get_ParentObj().m_Channels.begin(); get_ParentObj().m_Channels.end() != it; )
	{
		Channel& c = (it++)->get_ParentObj();
		c.OnRolledBack();
	}
}

void Client::NodeEvts::OnMsg(proto::BbsMsg&& msg)
{
	if (msg.m_Message.empty())
		return;

	Blob blob;

	uint8_t* pMsg = &msg.m_Message.front();
	blob.n = static_cast<uint32_t>(msg.m_Message.size());

	if (!proto::Bbs::Decrypt(pMsg, blob.n, get_ParentObj().m_skBbs))
		return;

	blob.p = pMsg;
	get_ParentObj().OnMsg(std::move(blob));
}

void Client::OnMsg(Blob&& blob)
{
	Channel::Key key;
	Storage::Map dataIn;

	try {
		Deserializer der;
		der.reset(blob.p, blob.n);

		der & key.m_ID;
		der & Cast::Down<FieldMap>(dataIn);
	}
	catch (const std::exception&) {
		return;
	}

	Channel::Map::iterator it = m_Channels.find(key);
	if (m_Channels.end() == it)
	{
		WalletID wid;
		if (!dataIn.Get(wid, Channel::Codes::MyWid))
			return;

		Channel& c = AddChannel(key);
		c.m_widTrg = wid;

		it = Channel::Map::s_iterator_to(c.m_Key);
	}

	Channel& c = it->get_ParentObj();
	c.OnPeerData(dataIn);
	c.LogNewState();
	c.MaybeDelete();
}

void Client::Send(Channel& c, Serializer& ser)
{
	proto::FlyClient::RequestBbsMsg::Ptr pReq(new proto::FlyClient::RequestBbsMsg);
	c.m_widTrg.m_Channel.Export(pReq->m_Msg.m_Channel);

	ECC::NoLeak<ECC::Hash::Value> hvRandom;
	ECC::GenRandom(hvRandom.V);

	ECC::Scalar::Native nonce;
	m_pKdf->DeriveKey(nonce, hvRandom.V);

	if (proto::Bbs::Encrypt(pReq->m_Msg.m_Message, c.m_widTrg.m_Pk, nonce, ser.buffer().first, static_cast<uint32_t>(ser.buffer().second)))
	{
		// skip mining!
		pReq->m_Msg.m_TimePosted = getTimestamp();
		m_Conn.PostRequest(*pReq, m_NodeEvts);
	}
}



void Client::Initialize()
{
	m_Conn.Connect();

	Key::ID kid;
	kid.m_Type = Key::Type::Bbs;

	uintBigFor<uint64_t>::Type val;
	ECC::GenRandom(val);
	val.Export(kid.m_Idx);

	m_pKdf->DeriveKey(m_skBbs, kid);
	proto::Sk2Pk(m_Wid.m_Pk, m_skBbs);

	BbsChannel ch;
	m_Wid.m_Pk.ExportWord<0>(ch);
	ch %= proto::Bbs::s_MaxChannels;
	m_Wid.m_Channel = ch;

	//std::cout << "My address: " << to_string(wid) << std::endl;

	m_Conn.BbsSubscribe(ch, getTimestamp(), &m_NodeEvts);
}


bool Client::OpenChannel(const WalletID& widTrg, Amount nMy, Amount nTrg)
{
	Channel::Key key;
	ECC::GenRandom(key.m_ID);

	Channel& c = AddChannel(key);
	c.m_widTrg = widTrg;

	c.m_Params.m_hLockTime = 10;
	c.m_Params.m_Fee = 101;

	HeightRange hr;
	hr.m_Min = get_TipHeight();
	hr.m_Max = hr.m_Min + 30;

	if (c.Open(nMy, nTrg, hr))
		return true;

	c.MaybeDelete();
	return false;
}

struct TestDirector
	:public Node::IObserver
{
	Client m_pC[2];
	Node m_Node;

	virtual void OnTip(Height) = 0;

	virtual void OnSyncProgress() override {}
	virtual void OnStateChanged() override
	{
		Height h = m_Node.get_Processor().m_Cursor.m_ID.m_Height;
		std::cout << "H=" << h << std::endl;

		OnTip(h);
	}

	virtual void OnRolledBack(const Block::SystemState::ID& id) override
	{
		std::cout << "Rollback to  " << id.m_Height << std::endl;
	}

	void Stop()
	{
		io::Reactor::get_Current().stop();
	}

	void MakeTreasury();
	void Run();

};

void TestDirector::MakeTreasury()
{
	Treasury tres;
	Treasury::Parameters pars;
	pars.m_Bursts = 1;
	pars.m_Maturity0 = 1;
	pars.m_MaturityStep = 0;

	for (size_t i = 0; i < _countof(m_pC); i++)
	{
		CreateTestKdf(m_pC[i].m_pKdf, i);

		PeerID pid;
		ECC::Scalar::Native sk;
		Treasury::get_ID(*m_pC[i].m_pKdf, pid, sk);

		Treasury::Entry* pE = tres.CreatePlan(pid, 0, pars);
		pE->m_Request.m_vGroups.front().m_vCoins.front().m_Value = Rules::get().Emission.Value0 * 4;

		pE->m_pResponse.reset(new Treasury::Response);
		uint64_t nIndex = 1;
		pE->m_pResponse->Create(pE->m_Request, *m_pC[i].m_pKdf, nIndex);

		for (size_t iG = 0; iG < pE->m_pResponse->m_vGroups.size(); iG++)
		{
			const Treasury::Response::Group& g = pE->m_pResponse->m_vGroups[iG];
			for (size_t iC = 0; iC < g.m_vCoins.size(); iC++)
			{
				const Treasury::Response::Group::Coin& coin = g.m_vCoins[iC];
				Key::IDV kidv;
				if (coin.m_pOutput->Recover(0, *m_pC[i].m_pKdf, kidv))
					m_pC[i].m_Coins[kidv] = coin.m_pOutput->m_Incubation;
			}
		}
	}



	Treasury::Data data;
	data.m_sCustomMsg = "LN";
	tres.Build(data);

	Serializer ser;
	ser & data;

	ser.swap_buf(m_Node.m_Cfg.m_Treasury);

	ECC::Hash::Processor() << Blob(m_Node.m_Cfg.m_Treasury) >> Rules::get().TreasuryChecksum;
}

void TestDirector::Run()
{
	MakeTreasury();

	io::Reactor::Ptr pReactor(io::Reactor::create());
	io::Reactor::Scope scope(*pReactor);

	std::cout << std::endl << "----------------------------------------------------------" << std::endl;

	DeleteFile(g_sz);
	m_Node.m_Cfg.m_sPathLocal = g_sz;
	m_Node.m_Cfg.m_Listen.port(25005);
	m_Node.m_Cfg.m_Listen.ip(INADDR_ANY);
	m_Node.m_Cfg.m_MiningThreads = 1;
	m_Node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 1000;

	m_Node.m_Cfg.m_Dandelion.m_AggregationTime_ms = 0;
	m_Node.m_Cfg.m_Dandelion.m_OutputsMin = 0;

	{
		ECC::uintBig seed = 345U;
		m_Node.m_Keys.InitSingleKey(seed);
	}

	m_Node.m_Cfg.m_Observer = this;

	m_Node.Initialize();
	m_Node.m_PostStartSynced = true;

	m_pC[0].m_sName = "  -A- ";
	m_pC[1].m_sName = "  -B- ";

	for (size_t i = 0; i < _countof(m_pC); i++)
	{
		m_pC[i].m_Conn.m_Cfg.m_vNodes.push_back(m_Node.m_Cfg.m_Listen);
		m_pC[i].Initialize();
	}

	pReactor->run();
}



void Test()
{
	//	auto logger = Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG);

	Rules::get().pForks[1].m_Height = 1;
	Rules::get().FakePoW = true;
	Rules::get().UpdateChecksum();

	{
		struct Test1 :public TestDirector
		{
			virtual void OnTip(Height h)
			{
				switch (h)
				{
				case 3:
					std::cout << "Scenario: User A opens a channel to B" << std::endl;
					m_pC[0].OpenChannel(m_pC[1].m_Wid, 25000, 34000);
					break;

				case 7:
				case 11:
					std::cout << "Scenario: User A sends funds" << std::endl;
					{
						Client& cl = m_pC[0];
						for (Client::Channel::Map::iterator it = cl.m_Channels.begin(); cl.m_Channels.end() != it; it++)
							(it)->get_ParentObj().Transfer(1200);
					}
					break;

				case 15:
					std::cout << "Scenario: User B sends funds" << std::endl;
					{
						Client& cl = m_pC[1];
						for (Client::Channel::Map::iterator it = cl.m_Channels.begin(); cl.m_Channels.end() != it; it++)
							(it)->get_ParentObj().Transfer(300);
					}
					break;

				case 20:
					std::cout << "Scenario: B decides to close (fair)" << std::endl;
					{
						Client& cl = m_pC[1];
						for (Client::Channel::Map::iterator it = cl.m_Channels.begin(); cl.m_Channels.end() != it; )
							(it++)->get_ParentObj().Close();
					}
					break;

				case 50:
					Stop();
					break;
				}
			}
		};

		Test1().Run();
	}


	{
		struct Test2 :public TestDirector
		{
			virtual void OnTip(Height h)
			{
				switch (h)
				{
				case 3:
					std::cout << "Scenario: User A opens a channel to B" << std::endl;
					m_pC[0].OpenChannel(m_pC[1].m_Wid, 25000, 34000);
					break;

				case 9:
					std::cout << "Scenario: User A sends funds" << std::endl;
					{
						Client& cl = m_pC[0];
						for (Client::Channel::Map::iterator it = cl.m_Channels.begin(); cl.m_Channels.end() != it; it++)
							(it)->get_ParentObj().Transfer(1200);
					}
					break;

				case 15:
					std::cout << "Scenario: User B sends funds" << std::endl;
					{
						Client& cl = m_pC[1];
						for (Client::Channel::Map::iterator it = cl.m_Channels.begin(); cl.m_Channels.end() != it; it++)
							(it)->get_ParentObj().Transfer(300);
					}
					break;

				case 21:
					std::cout << "Scenario: User B sends funds" << std::endl;
					{
						Client& cl = m_pC[1];
						for (Client::Channel::Map::iterator it = cl.m_Channels.begin(); cl.m_Channels.end() != it; it++)
							(it)->get_ParentObj().Transfer(600);
					}
					break;

				case 22:
					std::cout << "Scenario: User B attempts to cheat, Close using Revision=1, while negotiating about new transfer" << std::endl;
					{
						Client& cl = m_pC[1];
						cl.m_OverrideWithdrawal = 1;
						for (Client::Channel::Map::iterator it = cl.m_Channels.begin(); cl.m_Channels.end() != it; it++)
							(it)->get_ParentObj().Close();
					}
					break;


				case 45:
					Stop();
					break;
				}
			}
		};

		Test2().Run();
	}


	{
		struct Test3 :public TestDirector
		{
			virtual void OnTip(Height h)
			{
				switch (h)
				{
				case 3:
					std::cout << "Scenario: User A opens a channel to B" << std::endl;
					m_pC[0].OpenChannel(m_pC[1].m_Wid, 25000, 34000);
					break;

				case 9:
					std::cout << "Scenario: User A sends funds" << std::endl;
					{
						Client& cl = m_pC[0];
						for (Client::Channel::Map::iterator it = cl.m_Channels.begin(); cl.m_Channels.end() != it; it++)
							(it)->get_ParentObj().Transfer(1200);
					}
					break;

				case 17:
					std::cout << "Scenario: B sends funds, and asks for graceful channel closing" << std::endl;
					{
						Client& cl = m_pC[1];
						for (Client::Channel::Map::iterator it = cl.m_Channels.begin(); cl.m_Channels.end() != it; it++)
							(it)->get_ParentObj().Transfer(700, true);
					}
					break;


				case 45:
					Stop();
					break;
				}
			}
		};

		Test3().Run();
	}

}


} // namespace Lightning
} // namespace beam





int main()
{
	beam::Lightning::Test();
	return 0;
}
