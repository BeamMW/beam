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


	struct MyFlyClient
		:public proto::FlyClient
//		,public proto::FlyClient::Request::IHandler
	{

		bool m_bRunning;
		Height m_hRolledTo;

		Block::SystemState::HistoryMap m_Hist;

		//virtual void OnComplete(Request& r) override
		//{
		//
		//}

        virtual void OnNewTip() override
        {
            // tip already added
        }

        virtual void OnRolledBack() override
        {
            // reversed states are already removed
            m_hRolledTo = m_Hist.m_Map.empty() ? 0 : m_Hist.m_Map.rbegin()->first;
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
