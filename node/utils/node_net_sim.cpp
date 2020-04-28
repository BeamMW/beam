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

} // namespace beam





int main(int argc, char* argv[])
{
    using namespace beam;

    auto [options, visibleOptions] = createOptionsDescription(GENERAL_OPTIONS);

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

    if (vm.count(cli::HELP))
    {
        std::cout << options << std::endl;
        return 0;
    }

    Rules::get().UpdateChecksum();

    return 0;
}
