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
// limitations under the License

#include "node_model.h"
#include "app_model.h"
#include "node/node.h"
#include <mutex>

#include "pow/external_pow.h"
#include "3rdparty/opencl-miner/beamStratum.h"
#include "3rdparty/opencl-miner/clHost.h"

using namespace beam;
using namespace beam::io;
using namespace std;

void NodeModel::runOpenclMiner()
{
    vector<int32_t> devices{1};
    bool cpuMine = false;

    LOG_DEBUG() << "runOpenclMiner()";

    beamMiner::beamStratum myStratum("127.0.0.1", "10008", "00000000", true);
    beamMiner::clHost myClHost;

    LOG_INFO() << "Setup OpenCL devices:";
    LOG_INFO() << "=====================";

    myClHost.setup(&myStratum, devices, cpuMine);

    LOG_INFO() << "Waiting for work from stratum:";
    LOG_INFO() << "==============================";

    myStratum.startWorking();

    while (!myStratum.hasWork()) {
        this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    LOG_INFO() << "Start mining:";
    LOG_INFO() << "=============";

    myClHost.startMining();
}
