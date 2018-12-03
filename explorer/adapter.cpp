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
#include "core/serialization_adapters.h"
#include "http/http_msg_creator.h"
#include "http/http_json_serializer.h"
#include "nlohmann/json.hpp"
#include "utility/helpers.h"
#include "utility/logger.h"

namespace beam { namespace explorer {

namespace {

static const size_t PACKER_FRAGMENTS_SIZE = 4096;
static const size_t CACHE_DEPTH = 100000;

const char* hash_to_hex(char* buf, const Merkle::Hash& hash) {
    return to_hex(buf, hash.m_pData, 32);
}

const char* uint256_to_hex(char* buf, const ECC::uintBig& n) {
    char* p = to_hex(buf + 2, n.m_pData, 32);
    while (p && *p == '0') ++p;
    if (*p == '\0') --p;
    *--p = 'x';
    *--p = '0';
    return p;
}

const char* difficulty_to_hex(char* buf, const Difficulty& d) {
    ECC::uintBig raw;
    d.Unpack(raw);
    return uint256_to_hex(buf, raw);
}

struct ResponseCache {
    io::SharedBuffer status;
    std::map<Height, io::SharedBuffer> blocks;
    Height currentHeight=0;
    Height lowHorizon=0;

    explicit ResponseCache(size_t depth) : _depth(depth)
    {}

    void compact() {
        if (blocks.empty() || currentHeight <= _depth) return;
        Height horizon = currentHeight - _depth;
        if (blocks.rbegin()->first < horizon) {
            blocks.clear();
            return;
        }
        auto b = blocks.begin();
        auto it = b;
        while (it != blocks.end()) {
            if (it->first >= horizon) break;
        }
        blocks.erase(b, it);
    }

    bool get_block(io::SerializedMsg& out, Height h) {
        const auto& it = blocks.find(h);
        if (it == blocks.end()) return false;
        out.push_back(it->second);
        return true;
    }

    void put_block(Height h, const io::SharedBuffer& body) {
        if (currentHeight - h > _depth) return;
        compact();
        blocks[h] = body;
    }

private:
    size_t _depth;
};

using nlohmann::json;

} //namespace

/// Explorer server backend, gets callback on status update and returns json messages for server
class Adapter : public INodeObserver, public IAdapter {
public:
    Adapter(Node& node) :
        _packer(PACKER_FRAGMENTS_SIZE),
        _nodeBackend(node.get_Processor()),
        _statusDirty(true),
        _nodeIsSyncing(true),
        _cache(CACHE_DEPTH)
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
        static const char* s = "[,]";
        io::SharedBuffer buf(s, 3);
        _leftBrace = buf;
        _leftBrace.size = 1;
        _comma = buf;
        _comma.size = 1;
        _comma.data ++;
        _rightBrace = buf;
        _rightBrace.size = 1;
        _rightBrace.data += 2;
    }

    /// Returns body for /status request
    void OnSyncProgress(int done, int total) override {
        bool isSyncing = (done != total);
        if (isSyncing != _nodeIsSyncing) {
            _statusDirty = true;
            _nodeIsSyncing = isSyncing;
        }
        if (_nextHook) _nextHook->OnSyncProgress(done, total);
    }

    void OnStateChanged() override {
        const auto& cursor = _nodeBackend.m_Cursor;
        _cache.currentHeight = cursor.m_ID.m_Height;
        _cache.lowHorizon = cursor.m_LoHorizon;
        _statusDirty = true;
        if (_nextHook) _nextHook->OnStateChanged();
    }

    bool get_status(io::SerializedMsg& out) override {
        if (_statusDirty) {
            const auto& cursor = _nodeBackend.m_Cursor;

            _cache.currentHeight = cursor.m_Sid.m_Height;
            _cache.lowHorizon = cursor.m_LoHorizon;

            char buf[80];

            _sm.clear();
            if (!serialize_json_msg(
                _sm,
                _packer,
                json{
                    { "timestamp", cursor.m_Full.m_TimeStamp },
                    { "height", _cache.currentHeight },
                    { "low_horizon", cursor.m_LoHorizon },
                    { "hash", hash_to_hex(buf, cursor.m_ID.m_Hash) },
                    { "chainwork",  uint256_to_hex(buf, cursor.m_Full.m_ChainWork) }
                }
            )) {
                return false;
            }

            _cache.status = io::normalize(_sm, false);
            _statusDirty = false;
            _sm.clear();
        }
        out.push_back(_cache.status);
        return true;
    }

    bool extract_row(Height height, uint64_t& row, uint64_t* prevRow) {
        NodeDB& db = _nodeBackend.get_DB();
        NodeDB::WalkerState ws(db);
        db.EnumStatesAt(ws, height);
        while (true) {
            if (!ws.MoveNext()) {
                return false;
            }
            if (NodeDB::StateFlags::Active & db.GetStateFlags(ws.m_Sid.m_Row)) {
                row = ws.m_Sid.m_Row;
                break;
            }
        }
        if (prevRow) {
            *prevRow = row;
            if (!db.get_Prev(*prevRow)) {
                *prevRow = 0;
            }
        }
        return true;
    }

    bool extract_block_from_row(json& out, uint64_t row) {
        NodeDB& db = _nodeBackend.get_DB();

        Block::SystemState::Full blockState;
        bool ok = true;
        try {
            db.get_State(row, blockState);
        } catch (...) {
            ok = false;
        }

        Block::SystemState::ID id;
        blockState.get_ID(id);

        Block::Body block;
        ByteBuffer bbP, bbE;
        if (ok) {
            ByteBuffer rollbackBuf;
            db.GetStateBlock(row, &bbP, &bbE, &rollbackBuf);
            if (bbP.empty()) {
                ok = false;
            }
            if (!rollbackBuf.empty()) {
                LOG_DEBUG() << to_hex(rollbackBuf.data(), rollbackBuf.size());
            }
        }

        if (ok) {
            try {
                NodeProcessor::ReadBody(block, bbP, bbE);
            }
            catch (const std::exception&) {
                LOG_WARNING() << "Block deserialization failed at " << blockState.m_Height;
                ok = false;
            }
        }

        if (ok) {
            char buf[80];

            json inputs = json::array();
            for (const auto &v : block.m_vInputs) {
                inputs.push_back(
                json{
                    {"commitment", uint256_to_hex(buf, v->m_Commitment.m_X)},
                    {"maturity",   v->m_Maturity}
                }
                );
            }

            json outputs = json::array();
            for (const auto &v : block.m_vOutputs) {
                outputs.push_back(
                json{
                    {"commitment", uint256_to_hex(buf, v->m_Commitment.m_X)},
                    {"maturity",   v->m_Maturity},
                    {"coinbase",   v->m_Coinbase},
                    {"incubation", v->m_Incubation}
                }
                );
            }

            json kernels = json::array();
            for (const auto &v : block.m_vKernels) {
                kernels.push_back(
                json{
                    {"fee", v->m_Fee},
                }
                );
            }

            out = json{
                {"found",      true},
                {"timestamp",  blockState.m_TimeStamp},
                {"height",     blockState.m_Height},
                {"hash",       hash_to_hex(buf, id.m_Hash)},
                {"prev",       hash_to_hex(buf, blockState.m_Prev)},
                {"difficulty", blockState.m_PoW.m_Difficulty.ToFloat()},
                {"chainwork",  uint256_to_hex(buf, blockState.m_ChainWork)},
                {"subsidy",    Rules::get_Emission(blockState.m_Height)},
                {"inputs",     inputs},
                {"outputs",    outputs},
                {"kernels",    kernels}
            };

            LOG_DEBUG() << out;
        }
        return ok;
    }

    bool extract_block(json& out, Height height, uint64_t& row, uint64_t* prevRow) {
        bool ok = true;
        if (row == 0) {
            ok = extract_row(height, row, prevRow);
        } else if (prevRow != 0) {
            *prevRow = row;
            if (!_nodeBackend.get_DB().get_Prev(*prevRow)) {
                *prevRow = 0;
            }
        }
        return ok && extract_block_from_row(out, row);
    }

    bool get_block_impl(io::SerializedMsg& out, uint64_t height, uint64_t& row, uint64_t* prevRow) {
        if (_cache.get_block(out, height)) {
            if (prevRow && row > 0) {
                extract_row(height, row, prevRow);
            }
            return true;
        }

        io::SharedBuffer body;
        bool blockAvailable = (/*height >= _cache.lowHorizon && */height <= _cache.currentHeight);
        if (blockAvailable) {
            json j;
            if (!extract_block(j, height, row, prevRow)) {
                blockAvailable = false;
            } else {
                _sm.clear();
                if (serialize_json_msg(_sm, _packer, j)) {
                    body = io::normalize(_sm, false);
                    _cache.put_block(height, body);
                } else {
                    return false;
                }
                _sm.clear();
            }
        }

        if (blockAvailable) {
            out.push_back(body);
            return true;
        }

        return serialize_json_msg(out, _packer, json{ { "found", false}, {"height", height } });
    }

    bool get_block(io::SerializedMsg& out, uint64_t height) override {
        uint64_t row=0;
        return get_block_impl(out, height, row, 0);
    }

    bool get_blocks(io::SerializedMsg& out, uint64_t startHeight, uint64_t n) override {
        static const uint64_t maxElements = 100;
        if (n > maxElements) n = maxElements;
        else if (n==0) n=1;
        Height endHeight = startHeight + n - 1;
        out.push_back(_leftBrace);
        uint64_t row = 0;
        uint64_t prevRow = 0;
        for (;;) {
            bool ok = get_block_impl(out, endHeight, row, &prevRow);
            if (!ok) return false;
            if (endHeight == startHeight) {
                break;
            }
            out.push_back(_comma);
            row = prevRow;
            --endHeight;
        }
        out.push_back(_rightBrace);
        return true;
    }

    HttpMsgCreator _packer;

    // node db interface
    NodeProcessor& _nodeBackend;

    // helper fragments
    io::SharedBuffer _leftBrace, _comma, _rightBrace;

    // If true then status boby needs to be refreshed
    bool _statusDirty;

    // True if node is syncing at the moment
    bool _nodeIsSyncing;

    // node observers chain
    INodeObserver** _hook;
    INodeObserver* _nextHook;

    ResponseCache _cache;

    io::SerializedMsg _sm;
};

IAdapter::Ptr create_adapter(Node& node) {
    return IAdapter::Ptr(new Adapter(node));
}

}} //namespaces

