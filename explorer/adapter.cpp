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
#include "node/node.h"
#include "p2p/stratum.h"
#include "p2p/http_msg_creator.h"
#include "utility/nlohmann/json.hpp"
#include "utility/logger.h"

namespace beam { namespace explorer {

namespace {

io::SharedBuffer get_status_impl(NodeProcessor& np, HttpMsgCreator& packer, bool isSyncing, Height& currentHeight) {
    const auto& cursor = np.m_Cursor;

    currentHeight = cursor.m_Sid.m_Height;

    LOG_DEBUG() << TRACE(currentHeight) << TRACE(cursor.m_Sid.m_Row) << TRACE(isSyncing);

    return io::SharedBuffer();
}

} //namespace

/// Explorer server backend, gets callback on status update and returns json messages for server
class Adapter : public INodeObserver, public IAdapter {
public:
    Adapter(Node& node) :
        _nodeBackend(node.get_Processor()),
        _statusDirty(true)
    {
        init_helper_fragments();
        _hook = &node.m_Cfg.m_Observer;
        _nextHook = *_hook;
        *_hook = this;
    }

    virtual ~Adapter() {
        if (_nextHook) *_hook = _nextHook;
    }

private:
    void init_helper_fragments() {
        // TODO
    }

    /// Returns body for /status request
    void OnSyncProgress(int done, int total) override {
        _nodeIsSyncing = (done != total);
        if (_nextHook) _nextHook->OnSyncProgress(done, total);
    }

    void OnStateChanged() override {
        // TODO

        const auto& cursor = _nodeBackend.m_Cursor;
        _currentHeight = cursor.m_ID.m_Height;


        LOG_INFO() << TRACE(_currentHeight)
            << "\n" << TRACE(cursor.m_ID)
            << "\n" << TRACE(cursor.m_Full.m_Height)
            << "\n" << TRACE(cursor.m_Full.m_Prev)
            << "\n" << TRACE(cursor.m_Full.m_ChainWork)
            << "\n" << TRACE(cursor.m_Full.m_Definition)
            << "\n" << TRACE(cursor.m_Full.m_TimeStamp)
            << "\n" << TRACE(cursor.m_Full.m_PoW.m_Nonce)
            << "\n" << TRACE(cursor.m_Full.m_PoW.m_Difficulty)
        ;

        _statusDirty = true;
        if (_nextHook) _nextHook->OnStateChanged();
    }

    const io::SharedBuffer& get_status(HttpMsgCreator& packer) override {
        if (_statusDirty) {
            _statusBody = get_status_impl(_nodeBackend, packer, _nodeIsSyncing, _currentHeight);
            _statusDirty = false;
        }
        return _statusBody;
    }

    const io::SharedBuffer& get_block(HttpMsgCreator& packer, uint64_t height) override {
        // TODO
        return _statusBody;
    }

    void get_blocks(HttpMsgCreator& packer, io::SerializedMsg& out, uint64_t startHeight, uint64_t endHeight) override {
        // TODO

    }

    // node db interface
    NodeProcessor& _nodeBackend;

    // helper fragments to pack json arrays
    io::SharedBuffer _leftBrace, _comma, _rightBrace;

    // body for status request
    io::SharedBuffer _statusBody;

    // If true then status boby needs to be refreshed
    bool _statusDirty;

    // True if node is syncing at the moment
    bool _nodeIsSyncing;

    // node observers chain
    INodeObserver** _hook;
    INodeObserver* _nextHook;

    Height _currentHeight;
};

IAdapter::Ptr create_adapter(Node& node) {
    return IAdapter::Ptr(new Adapter(node));
}

}} //namespaces

