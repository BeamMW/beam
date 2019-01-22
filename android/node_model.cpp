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
#include "node/node.h"
#include <mutex>

#include "pow/external_pow.h"

#include <boost/filesystem.hpp>
#ifdef  BEAM_USE_GPU
#include "utility/gpu/gpu_tools.h"
#endif //  BEAM_USE_GPU
#include "utility/logger.h"


using namespace beam;
using namespace beam::io;
using namespace std;

NodeModel::NodeModel(const std::string& appPath)
    : m_nodeClient(this)
    , m_appPath(appPath)
{
}

void NodeModel::setKdf(beam::Key::IKdf::Ptr kdf)
{
    m_nodeClient.setKdf(kdf);
}

void NodeModel::startNode()
{
    m_nodeClient.startNode();
}

void NodeModel::stopNode()
{
    m_nodeClient.stopNode();
}

void NodeModel::start()
{
    m_nodeClient.start();
}

bool NodeModel::isNodeRunning() const
{
    return m_nodeClient.isNodeRunning();
}

void NodeModel::onSyncProgressUpdated(int done, int total)
{
}

void NodeModel::onStartedNode()
{
}

void NodeModel::onStoppedNode()
{
}

void NodeModel::onFailedToStartNode()
{
}

uint16_t NodeModel::getLocalNodePort()
{
    // default value
    return 10005;
}

std::string NodeModel::getLocalNodeStorage()
{
    return m_appPath + "/node.db";
}

unsigned int NodeModel::getLocalNodeMiningThreads()
{
    return 0;
}

std::string NodeModel::getTempDir()
{
    return m_appPath + "/temp";
}

std::vector<std::string> NodeModel::getLocalNodePeers()
{
    std::vector<std::string> result
    {
        #ifdef BEAM_TESTNET
        "ap-node01.testnet.beam.mw:8100",
        "ap-node02.testnet.beam.mw:8100",
        "ap-node03.testnet.beam.mw:8100",
        "eu-node01.testnet.beam.mw:8100",
        "eu-node02.testnet.beam.mw:8100",
        "eu-node03.testnet.beam.mw:8100",
        "us-node01.testnet.beam.mw:8100",
        "us-node02.testnet.beam.mw:8100",
        "us-node03.testnet.beam.mw:8100"
 #else
        "eu-node01.masternet.beam.mw:8100",
        "eu-node02.masternet.beam.mw:8100",
        "eu-node03.masternet.beam.mw:8100",
        "eu-node04.masternet.beam.mw:8100"
#endif
    };

    return result;
}

#ifdef BEAM_USE_GPU
std::unique_ptr<IExternalPOW> NodeModel::getStratumServer()
{
    return nullptr;
}
#endif //  BEAM_USE_GPU
