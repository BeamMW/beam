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

#include <QThread>
#include <memory>
#include <atomic>
#include <condition_variable>
#include "core/block_crypt.h"
#include "node/node.h"
#include "utility/io/reactor.h"

class NodeModel : public QThread
{
    Q_OBJECT
public:
    NodeModel();
    ~NodeModel();

    void setKdf(beam::Key::IKdf::Ptr);
    void startNode();
    void stopNode();

    bool isNodeRunning() const;
private:
    void run() override;

    void runLocalNode();
    void runOpenclMiner();
signals:
    void syncProgressUpdated(int done, int total);
    void startedNode();
    void stoppedNode();
private: 
    std::weak_ptr<beam::io::Reactor> m_reactor;
    std::atomic<bool> m_shouldStartNode;
    std::atomic<bool> m_shouldTerminateModel;
    std::atomic<bool> m_isRunning;
    std::condition_variable m_waiting;
    beam::Key::IKdf::Ptr m_pKdf;
};