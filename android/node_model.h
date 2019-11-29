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

#pragma once

#include <memory>
#include <atomic>
#include <condition_variable>
#include "core/block_crypt.h"
#include "node/node.h"
#include "node/node_client.h"

class NodeModel
    : private beam::INodeClientObserver
{
public:

    NodeModel(const std::string& appPath);

    void setKdf(beam::Key::IKdf::Ptr);
    void startNode();
    void stopNode();

    void start();

    bool isNodeRunning() const;

protected:
    void onInitProgressUpdated(uint64_t done, uint64_t total) override;
    void onSyncProgressUpdated(int done, int total) override;
    void onNodeCreated() override;
    void onNodeDestroyed() override;
    void onStartedNode() override;
    void onStoppedNode() override;
    // void onFailedToStartNode() override;
    void onFailedToStartNode(beam::io::ErrorCode errorCode) override;
    void onSyncError(beam::Node::IObserver::Error error) override;

    uint16_t getLocalNodePort() override;
    std::string getLocalNodeStorage() override;
    std::string getTempDir() override;
    std::vector<std::string> getLocalNodePeers() override;

    void onNodeThreadFinished() override;

private:
    beam::NodeClient m_nodeClient;
    std::string m_appPath;
};
