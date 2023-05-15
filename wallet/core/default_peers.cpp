// Copyright 2019 The Beam Team
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

#include "default_peers.h"
#include "../../core/block_crypt.h"

namespace beam
{
    void Arr2Vec(std::vector<std::string>& res, const char* psz[], uint32_t n)
    {
        res.reserve(n);
        for (uint32_t i = 0; i < n; i++)
            res.emplace_back(psz[i]);
    }

    std::vector<std::string> getDefaultPeers()
    {
        std::vector<std::string> result;

        switch (Rules::get().m_Network)
        {
        case Rules::Network::testnet:
            {
                static const char* psz[] = {
                    "us-nodes.testnet.beam.mw:8100",
                    "eu-nodes.testnet.beam.mw:8100",
                    "ap-nodes.testnet.beam.mw:8100"
                };
                Arr2Vec(result, psz, _countof(psz));
            }
            break;

        case Rules::Network::mainnet:
            {
                static const char* psz[] = {
                    "ap-nodes.mainnet.beam.mw:8100",
                    "eu-nodes.mainnet.beam.mw:8100",
                    "us-nodes.mainnet.beam.mw:8100",
                    "ap-hk-nodes.mainnet.beam.mw:8100",
                    "45.63.100.240:10127",
                    "usa.raskul.com:10174",
                    "canada.raskul.com:10174",
                    "japan.raskul.com:10127",
                    "india.raskul.com:10127",
                    "oz.raskul.com:10127",
                };
                Arr2Vec(result, psz, _countof(psz));
            }
            break;

        case Rules::Network::dappnet:
            {
                static const char* psz[] = {
                    "eu-node01.dappnet.beam.mw:8100",
                    "eu-node02.dappnet.beam.mw:8100",
                    "eu-node03.dappnet.beam.mw:8100"
                };
                Arr2Vec(result, psz, _countof(psz));
            }
            break;

        case Rules::Network::masternet:
            {
                static const char* psz[] = {
                    "eu-node01.masternet.beam.mw:8100",
                    "eu-node02.masternet.beam.mw:8100",
                    "eu-node03.masternet.beam.mw:8100",
                    "eu-node04.masternet.beam.mw:8100"
                };
                Arr2Vec(result, psz, _countof(psz));
            }
            break;
        }

        return result;
    }

    std::vector<std::string> getOutdatedDefaultPeers()
    {
        std::vector<std::string> result;

        switch (Rules::get().m_Network)
        {
        case Rules::Network::testnet:
            {
                static const char* psz[] = {
                    "ap-node01.testnet.beam.mw:8100",
                    "ap-node02.testnet.beam.mw:8100",
                    "ap-node03.testnet.beam.mw:8100",
                    "eu-node01.testnet.beam.mw:8100",
                    "eu-node02.testnet.beam.mw:8100",
                    "eu-node03.testnet.beam.mw:8100",
                    "us-node01.testnet.beam.mw:8100",
                    "us-node02.testnet.beam.mw:8100",
                    "us-node03.testnet.beam.mw:8100"
                };
                Arr2Vec(result, psz, _countof(psz));
            }
            break;

        case Rules::Network::mainnet:
            {
                static const char* psz[] = {
                    "eu-node01.mainnet.beam.mw:8100",
                    "eu-node02.mainnet.beam.mw:8100",
                    "eu-node03.mainnet.beam.mw:8100",
                    "us-node01.mainnet.beam.mw:8100",
                    "us-node02.mainnet.beam.mw:8100",
                    "us-node03.mainnet.beam.mw:8100",
                    "us-node04.mainnet.beam.mw:8100",
                    "ap-node01.mainnet.beam.mw:8100",
                    "ap-node02.mainnet.beam.mw:8100",
                    "ap-node03.mainnet.beam.mw:8100",
                    "ap-node04.mainnet.beam.mw:8100",
                    "eu-node04.mainnet.beam.mw:8100"
                };
                Arr2Vec(result, psz, _countof(psz));
            }
            break;
        }

        return result;
    }
}
