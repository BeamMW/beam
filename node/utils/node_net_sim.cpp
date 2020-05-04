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
#include "../../mnemonic/mnemonic.h"
#include "../../utility/cli/options.h"
#include "../../core/fly_client.h"
#include "../../core/treasury.h"
#include "../../core/serialization_adapters.h"
#include <boost/core/ignore_unused.hpp>

#define LOG_VERBOSE_ENABLED 0
#include "utility/logger.h"

namespace beam {

bool ReadSeed(Key::IKdf::Ptr& pKdf, const char* szSeed)
{
    std::vector<std::string> vWords;
    while (true)
    {
        const char* p = strchr(szSeed, ';');
        if (!p)
            break;
        if (p > szSeed)
            vWords.push_back(std::string(szSeed, p));

        szSeed = p + 1;
    };

    if (vWords.size() != beam::WORD_COUNT)
        return false;

    beam::ByteBuffer buf = beam::decodeMnemonic(vWords);

    ECC::Hash::Value hvSeed;
    ECC::Hash::Processor() << beam::Blob(buf) >> hvSeed;

    pKdf = std::make_shared<ECC::HKdf>();
    ECC::HKdf& hkdf = Cast::Up<ECC::HKdf>(*pKdf);

    hkdf.Generate(hvSeed);
    return true;
}

struct Context
{
    Key::IKdf::Ptr m_pKdf;

    struct Txo
    {
        struct Cid
            :public boost::intrusive::set_base_hook<>
        {
            CoinID m_Value;
            bool operator < (const Cid& cid) const { return m_Value < cid.m_Value; }

            IMPLEMENT_GET_PARENT_OBJ(Txo, m_Cid)
        } m_Cid;

        struct HeightNode
            :public boost::intrusive::set_base_hook<>
        {
            Height m_Confirmed = MaxHeight;
            Height m_Spent = MaxHeight;

            Height get() const { return std::min(m_Confirmed, m_Spent); }
            bool operator < (const HeightNode& x) const { return get() < x.get(); }

            IMPLEMENT_GET_PARENT_OBJ(Txo, m_Height)
        } m_Height;

        Height m_Maturity;
    };

    typedef boost::intrusive::multiset<Txo::HeightNode> HeightMap;
    typedef boost::intrusive::multiset<Txo::Cid> CidMap;

    CidMap m_mapCid;
    HeightMap m_mapConfirmed;
    HeightMap m_mapSpent;

    struct EventsHandler
        :public proto::FlyClient::Request::IHandler
    {
        Height m_hEvents = 0;
        proto::FlyClient::Request::Ptr m_pReq;

        virtual void OnComplete(proto::FlyClient::Request& r_) override
        {
            assert(&r_ == m_pReq.get());
            m_pReq.reset();

            proto::FlyClient::RequestEvents& r = Cast::Up<proto::FlyClient::RequestEvents>(r_);

            struct MyParser :public proto::Event::IGroupParser
            {
                Context& m_This;
                MyParser(Context& x) :m_This(x) {}

                virtual void OnEvent(proto::Event::Base& evt) override
                {
                    if (proto::Event::Type::Utxo == evt.get_Type())
                        return OnEventType(Cast::Up<proto::Event::Utxo>(evt));
                    if (proto::Event::Type::Shielded == evt.get_Type())
                        return OnEventType(Cast::Up<proto::Event::Shielded>(evt));
                    if (proto::Event::Type::AssetCtl == evt.get_Type())
                        return OnEventType(Cast::Up<proto::Event::AssetCtl>(evt));
                }

                void OnEventType(proto::Event::Utxo& evt)
                {
                    if (MaxHeight == m_Height)
                        return; // it shouldn't be MaxHeight anyway!

                    Txo::Cid cid;
                    cid.m_Value = evt.m_Cid;

                    CidMap::iterator it = m_This.m_mapCid.find(cid);

                    if (proto::Event::Flags::Add & evt.m_Flags)
                    {
                        if (m_This.m_mapCid.end() == it)
                        {
                            Txo* pTxo = new Txo;
                            pTxo->m_Cid.m_Value = cid.m_Value;
                            m_This.m_mapCid.insert(pTxo->m_Cid);

                            pTxo->m_Height.m_Confirmed = m_Height;
                            m_This.m_mapConfirmed.insert(pTxo->m_Height);

                            pTxo->m_Maturity = evt.m_Maturity;
                        }
                    }
                    else
                    {
                        if (m_This.m_mapCid.end() != it)
                        {
                            Txo& txo = it->get_ParentObj();
                            if (MaxHeight == txo.m_Height.m_Spent)
                            {
                                m_This.m_mapConfirmed.erase(HeightMap::s_iterator_to(txo.m_Height));
                                txo.m_Height.m_Spent = m_Height;
                                m_This.m_mapSpent.insert(txo.m_Height);
                                
                                // update txs?
                            }
                        }
                    }
                }

                void OnEventType(proto::Event::Shielded& evt)
                {
                }

                void OnEventType(proto::Event::AssetCtl& evt)
                {
                }

            } p(get_ParentObj());

            uint32_t nCount = p.Proceed(r.m_Res.m_Events);

            Height hTip = get_ParentObj().m_FlyClient.get_Height();

            if (nCount < proto::Event::s_Max)
                m_hEvents = hTip + 1;
            else
            {
                m_hEvents = p.m_Height + 1;
                if (m_hEvents < hTip + 1)
                    AskEventsStrict();
            }

            if (!m_pReq)
                get_ParentObj().OnEventsHandled();
        }

        void Abort()
        {
            if (m_pReq) {
                m_pReq->m_pTrg = nullptr;
                m_pReq.reset();
            }
        }

        ~EventsHandler() {
            Abort();
        }

        void AskEventsStrict()
        {
            assert(!m_pReq);

            m_pReq = new proto::FlyClient::RequestEvents;
            Cast::Up<proto::FlyClient::RequestEvents>(*m_pReq).m_Msg.m_HeightMin = m_hEvents;

            get_ParentObj().m_Network.PostRequest(*m_pReq, *this);
        }

        IMPLEMENT_GET_PARENT_OBJ(Context, m_EventsHandler)

    } m_EventsHandler;

	struct MyFlyClient
		:public proto::FlyClient
	{

		Block::SystemState::HistoryMap m_Hist;

        Height get_Height() const {
            return m_Hist.m_Map.empty() ? 0 : m_Hist.m_Map.rbegin()->first;
        }

        virtual void OnNewTip() override
        {
            get_ParentObj().OnNewTip();
        }

        virtual void OnRolledBack() override
        {
            get_ParentObj().OnRolledBack();
        }

        virtual void get_Kdf(Key::IKdf::Ptr& pKdf) override
        {
            pKdf = get_ParentObj().m_pKdf;
        }

        virtual void get_OwnerKdf(Key::IPKdf::Ptr& pPKdf) override
        {
            pPKdf = get_ParentObj().m_pKdf;
        }

        virtual Block::SystemState::IHistory& get_History() override
        {
            return m_Hist;
        }

      
        IMPLEMENT_GET_PARENT_OBJ(Context, m_FlyClient)

    } m_FlyClient;

    proto::FlyClient::NetworkStd m_Network;

    Context()
        :m_Network(m_FlyClient)
    {
    }

    ~Context()
    {
        while (!m_mapCid.empty())
            DeleteTxo(m_mapConfirmed.begin()->get_ParentObj());
    }

    void DeleteTxo(Txo& txo)
    {
        if (MaxHeight != txo.m_Height.m_Spent)
            m_mapSpent.erase(HeightMap::s_iterator_to(txo.m_Height));
        else
        {
            if (MaxHeight != txo.m_Height.m_Confirmed)
                m_mapConfirmed.erase(HeightMap::s_iterator_to(txo.m_Height));
        }

        m_mapCid.erase(CidMap::s_iterator_to(txo.m_Cid));

        delete &txo;
    }

    void OnRolledBack()
    {
        Height h = m_FlyClient.get_Height();

        while (!m_mapSpent.empty())
        {
            Txo& txo = m_mapSpent.rbegin()->get_ParentObj();
            assert(txo.m_Height.m_Spent != MaxHeight);
            assert(txo.m_Height.m_Confirmed != MaxHeight);

            if (txo.m_Height.m_Spent <= h)
                break;

            if (txo.m_Height.m_Confirmed > h)
                DeleteTxo(txo);
            else
            {
                txo.m_Height.m_Spent = MaxHeight;
                m_mapSpent.erase(HeightMap::s_iterator_to(txo.m_Height));
                m_mapConfirmed.insert(txo.m_Height);
            }
        }

        m_EventsHandler.Abort();
        std::setmin(m_EventsHandler.m_hEvents, h + 1);
    }

    void OnNewTip()
    {
        Height h = m_FlyClient.get_Height();

        if (h >= Rules::get().MaxRollback)
            ForgetOld(h - Rules::get().MaxRollback);

        if (!m_EventsHandler.m_pReq)
            m_EventsHandler.AskEventsStrict();
    }

    void ForgetOld(Height h)
    {
        while (!m_mapSpent.empty())
        {
            Txo& txo = m_mapSpent.begin()->get_ParentObj();
            assert(txo.m_Height.m_Spent != MaxHeight);
            assert(txo.m_Height.m_Confirmed != MaxHeight);

            if (txo.m_Height.m_Spent > h)
                break;

            DeleteTxo(txo);
        }
    }

    void OnEventsHandled()
    {
        // decide w.r.t. test logic
    }



};

uint16_t g_LocalNodePort = 16725;

void DoTest(Key::IKdf::Ptr& pKdf)
{
    Context ctx;
    ctx.m_pKdf = pKdf;
    ctx.m_Network.m_Cfg.m_vNodes.push_back(io::Address(INADDR_LOOPBACK, g_LocalNodePort));
    ctx.m_Network.Connect();


    io::Reactor::get_Current().run();
}


} // namespace beam


int main_Guarded(int argc, char* argv[])
{
    using namespace beam;

    io::Reactor::Ptr pReactor(io::Reactor::create());
    io::Reactor::Scope scope(*pReactor);
    io::Reactor::GracefulIntHandler gih(*pReactor);

    auto logger = beam::Logger::create(LOG_LEVEL_INFO, LOG_LEVEL_INFO);

    Key::IKdf::Ptr pKdf;

    Node node;
    node.m_Cfg.m_sPathLocal = "node_net_sim.db";

    bool bLocalMode = true;

    if (!bLocalMode)
    {
        auto [options, visibleOptions] = createOptionsDescription(0);
        boost::ignore_unused(visibleOptions);
        options.add_options()
            (cli::SEED_PHRASE, po::value<std::string>()->default_value(""), "seed phrase");

        po::variables_map vm = getOptions(argc, argv, "node_net_sim.cfg", options, true);

        if (!ReadSeed(pKdf, vm[cli::SEED_PHRASE].as<std::string>().c_str()))
        {
            std::cout << options << std::endl;
            return 0;
        }

        std::vector<std::string> vPeers = getCfgPeers(vm);

        uint16_t nPort = vm[cli::PORT].as<uint16_t>();
        if (nPort)
            g_LocalNodePort = nPort;

        for (size_t i = 0; i < vPeers.size(); i++)
        {
            io::Address addr;
            if (addr.resolve(vPeers[i].c_str()))
            {
                if (!addr.port())
                    addr.port(nPort);

                node.m_Cfg.m_Connect.push_back(addr);
            }
        }

    }
    else
    {
        ECC::HKdf::Create(pKdf, 225U);

        PeerID pid;
        ECC::Scalar::Native sk;
        Treasury::get_ID(*pKdf, pid, sk);

        Treasury tres;
        Treasury::Parameters pars;
        pars.m_Bursts = 1;
        Treasury::Entry* pE = tres.CreatePlan(pid, Rules::get().Emission.Value0 / 5, pars);

        pE->m_pResponse.reset(new Treasury::Response);
        uint64_t nIndex = 1;
        BEAM_VERIFY(pE->m_pResponse->Create(pE->m_Request, *pKdf, nIndex));

        Treasury::Data data;
        data.m_sCustomMsg = "test treasury";
        tres.Build(data);

        beam::Serializer ser;
        ser & data;

        ser.swap_buf(node.m_Cfg.m_Treasury);

        ECC::Hash::Processor() << Blob(node.m_Cfg.m_Treasury) >> Rules::get().TreasuryChecksum;
        Rules::get().FakePoW = true;
        Rules::get().pForks[1].m_Height = 1;
        Rules::get().pForks[2].m_Height = 2;

        node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 3000;
        node.m_Cfg.m_MiningThreads = 1;

        beam::DeleteFile(node.m_Cfg.m_sPathLocal.c_str());
    }

    Rules::get().UpdateChecksum();

    node.m_Cfg.m_Listen.port(g_LocalNodePort);
    node.m_Cfg.m_Listen.ip(INADDR_ANY);

    node.m_Keys.SetSingleKey(pKdf);
    node.Initialize();

    DoTest(pKdf);

    return 0;
}

int main(int argc, char* argv[])
{
    int ret = 0;
    try
    {
        ret = main_Guarded(argc, argv);
    }
    catch (const std::exception & e)
    {
        std::cout << e.what() << std::endl;
        return 1;
    }

    return ret;
}
