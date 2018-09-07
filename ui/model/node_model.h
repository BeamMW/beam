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
#include "core/block_crypt.h"
#include "beam/node.h"
#include "utility/io/reactor.h"

class NodeModel : public QThread
                , public beam::INodeObserver
{
    Q_OBJECT
public:
    NodeModel(const ECC::NoLeak<ECC::uintBig>& seed);
    ~NodeModel();
private:
    void run() override;
    void OnSyncProgress(int done, int total) override;
signals:
    void syncProgressUpdated(int done, int total);
private: 
    std::weak_ptr<beam::io::Reactor> m_reactor;
    ECC::NoLeak<ECC::uintBig> m_seed;
};