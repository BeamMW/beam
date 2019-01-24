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

#include <boost/filesystem.hpp>
#ifdef  BEAM_USE_GPU
#include "utility/gpu/gpu_tools.h"
#endif //  BEAM_USE_GPU


using namespace beam;
using namespace beam::io;
using namespace std;

NodeModel::NodeModel()
    : m_nodeClient(this)
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
    emit syncProgressUpdated(done, total);
}

void NodeModel::onStartedNode()
{
    emit startedNode();
}

void NodeModel::onStoppedNode()
{
    emit stoppedNode();
}

void NodeModel::onFailedToStartNode()
{
    emit failedToStartNode(beam::wallet::ErrorType::InternalNodeStartFailed);
}

void NodeModel::onFailedToStartNode(io::ErrorCode errorCode)
{
    emit failedToStartNode(wallet::getWalletError(errorCode));
}

uint16_t NodeModel::getLocalNodePort()
{
    return AppModel::getInstance()->getSettings().getLocalNodePort();
}

std::string NodeModel::getLocalNodeStorage()
{
    return AppModel::getInstance()->getSettings().getLocalNodeStorage();
}

std::string NodeModel::getTempDir()
{
    return AppModel::getInstance()->getSettings().getTempDir();
}

std::vector<std::string> NodeModel::getLocalNodePeers()
{
    std::vector<std::string> result;

    auto peers = AppModel::getInstance()->getSettings().getLocalNodePeers();

    for (const auto& peer : peers)
    {
        result.push_back(peer.toStdString());
    }

    return result;
}

#ifdef BEAM_USE_GPU
std::unique_ptr<IExternalPOW> NodeModel::getStratumServer()
{
    auto& settings = AppModel::getInstance()->getSettings();
    GetSupportedCards();
    auto devices = settings.getMiningDevices();
    return settings.getUseGpu() && !devices.empty() ? IExternalPOW::create_opencl_solver(devices) : nullptr;
}
#endif //  BEAM_USE_GPU
