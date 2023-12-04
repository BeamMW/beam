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

#include "utility/io/buffer.h"
#include "utility/common.h"
#include "nlohmann/json.hpp"

namespace beam {

struct Node;
using nlohmann::json;

namespace explorer {

/// node->explorer adapter interface
struct IAdapter {

    enum struct Mode {
        Legacy,
        ExplicitType,
        AutoHtml,
    };

    Mode m_Mode = Mode::Legacy;

    using Ptr = std::unique_ptr<IAdapter>;

    virtual ~IAdapter() = default;

    /// Returns body for /status request
    virtual json get_status() = 0;
    virtual json get_block(uint64_t height) = 0;
    virtual json get_block_by_kernel(const Blob& key) = 0;
    virtual json get_blocks(uint64_t startHeight, uint64_t n) = 0;
    virtual json get_hdrs(uint64_t hMax, uint64_t nMax) = 0;
    virtual json get_peers() = 0;

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    virtual json get_swap_offers() = 0;
    virtual json get_swap_totals() = 0;
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
    virtual json get_contracts() = 0;
    virtual json get_contract_details(const Blob& id, Height hMin, Height hMax, uint32_t nMaxTxs) = 0;
    virtual json get_asset_history(uint32_t, Height hMin, Height hMax, uint32_t nMaxOps) = 0;
    virtual json get_assets_at(Height) = 0;
};

IAdapter::Ptr create_adapter(Node& node);

}} //namespaces
