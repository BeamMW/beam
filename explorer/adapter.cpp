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

#include "adapter.h"
#include "p2p/stratum.h"
#include "beam/node.h"
#include "utility/nlohmann/json.hpp"

namespace beam { namespace explorer {

/// Explorer server backend, gets callback on status update and returns json messages for server
class Adapter : public IAdapter, public INodeObserver {
public:
    Adapter(Node& node);

private:
    /// Returns body for /status request
    void OnSyncProgress(int done, int total) override;

    void OnStateChanged() override;

    const io::SharedBuffer& get_status(HttpMsgCreator& packer) override;

    const io::SharedBuffer& get_block(HttpMsgCreator& packer, uint64_t height) override;

    void get_blocks(HttpMsgCreator& packer, io::SerializedMsg& out, uint64_t startHeight, uint64_t endHeight) override;

    // status callback
    void on_status_changed();

    // node observers chain
    INodeObserver** _hook;
    INodeObserver* _nextHook;

    // node db interface
    NodeProcessor& _nodeBackend;

    // helper fragments to pack json arrays
    io::SharedBuffer _leftBrace, _comma, _rightBrace;

    // body for status request
    io::SharedBuffer _statusBody;

    // If true then status boby needs to be refreshed
    bool _statusDirty;
};

}} //namespaces

