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

namespace beam
{
    std::vector<std::string> getDefaultPeers()
    {
        std::vector<std::string> result
        {
#ifdef BEAM_TESTNET
            "us-nodes.testnet.beam.mw:8100",
            "eu-nodes.testnet.beam.mw:8100",
            "ap-nodes.testnet.beam.mw:8100"
#else
            "eu-node01.masternet.beam.mw:8100",
            "eu-node02.masternet.beam.mw:8100",
            "eu-node03.masternet.beam.mw:8100",
            "eu-node04.masternet.beam.mw:8100"
#endif
        };

        return result;
    }
}