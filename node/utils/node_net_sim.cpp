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

    };

    typedef boost::intrusive::multiset<Txo::HeightNode> HeightMap;
    typedef boost::intrusive::multiset<Txo::Cid> CidMap;

    CidMap m_mapCid;
    HeightMap m_mapConfirmed;
    HeightMap m_mapSpent;


	struct MyFlyClient
		:public proto::FlyClient
//		,public proto::FlyClient::Request::IHandler
	{

		bool m_bRunning;
		Height m_hRolledTo;

		Block::SystemState::HistoryMap m_Hist;

        Height get_Height() const {
            return m_Hist.m_Map.empty() ? 0 : m_Hist.m_Map.rbegin()->first;
        }

		//virtual void OnComplete(Request& r) override
		//{
		//
		//}

        virtual void OnNewTip() override
        {
            get_ParentObj().ForgetOld();
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
    }

    void ForgetOld()
    {
        Height h = m_FlyClient.get_Height();
        if (h < Rules::get().MaxRollback)
            return;
        h -= Rules::get().MaxRollback;

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




};

void DoTest(Key::IKdf::Ptr& pKdf)
{
    Node node;
    node.m_Cfg.m_sPathLocal = "node_net_sim.db";
    node.m_Keys.SetSingleKey(pKdf);
    node.Initialize();

    io::Reactor::get_Current().run();
}


} // namespace beam





int main(int argc, char* argv[])
{
    using namespace beam;

    auto [options, visibleOptions] = createOptionsDescription(0);
    boost::ignore_unused(visibleOptions);
    options.add_options()
        (cli::SEED_PHRASE, po::value<std::string>()->default_value(""), "seed phrase");

    po::variables_map vm;
    try
    {
        vm = getOptions(argc, argv, "node_net_sim.cfg", options, true);
    }
    catch (const std::exception & e)
    {
        std::cout << e.what() << std::endl;
        return 1;
    }

    Key::IKdf::Ptr pKdf;
    if (!ReadSeed(pKdf, vm[cli::SEED_PHRASE].as<std::string>().c_str()))
    {
        std::cout << options << std::endl;
        return 0;
    }

    Rules::get().UpdateChecksum();

    io::Reactor::Ptr pReactor(io::Reactor::create());
    io::Reactor::Scope scope(*pReactor);

    DoTest(pKdf);

    return 0;
}
