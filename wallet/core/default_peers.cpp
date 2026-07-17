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

        // In the WASM/browser build every connection must go over wss://, so the
        // default peers listen on the WebSocket port (:8200). The raw-TCP P2P
        // ports (:8100) used by native builds are unreachable from the browser.
#ifdef __EMSCRIPTEN__
        constexpr const char* kDefaultPort = ":8200";
#else
        constexpr const char* kDefaultPort = ":8100";
#endif

        auto addPeers = [&result](const char* hosts[], uint32_t n)
        {
            result.reserve(n);
            for (uint32_t i = 0; i < n; i++)
                result.emplace_back(std::string(hosts[i]) + kDefaultPort);
        };

        switch (Rules::get().m_Network)
        {
        case Rules::Network::testnet:
            {
                static const char* hosts[] = {
                    "us-nodes.testnet.beam.mw",
                    "eu-nodes.testnet.beam.mw",
                    "ap-nodes.testnet.beam.mw"
                };
                addPeers(hosts, _countof(hosts));
            }
            break;

        case Rules::Network::mainnet:
            {
#ifdef __EMSCRIPTEN__
                // us-nodes.mainnet.beam.mw has no WebSocket (:8200) endpoint, only
                // raw TCP (:8100). Listing it in the browser build causes endless
                // failed wss reconnect attempts, so restrict to hosts that serve wss.
                static const char* hosts[] = {
                    "eu-nodes.mainnet.beam.mw",
                };
#else
                static const char* hosts[] = {
                    "eu-nodes.mainnet.beam.mw",
                    "us-nodes.mainnet.beam.mw",
                };
#endif
                addPeers(hosts, _countof(hosts));
            }
            break;

        case Rules::Network::dappnet:
            {
                static const char* hosts[] = {
                    "eu-node01.dappnet.beam.mw",
                    "eu-node02.dappnet.beam.mw",
                    "eu-node03.dappnet.beam.mw"
                };
                addPeers(hosts, _countof(hosts));
            }
            break;

        case Rules::Network::masternet:
            {
                static const char* hosts[] = {
                    "eu-node01.masternet.beam.mw",
                    "eu-node02.masternet.beam.mw",
                    "eu-node03.masternet.beam.mw",
                    "eu-node04.masternet.beam.mw"
                };
                addPeers(hosts, _countof(hosts));
            }
            break;

        default:
            break; // suppress warning

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

        default:
            break; // suppress the warning
        }

        return result;
    }
}
