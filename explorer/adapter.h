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

namespace beam {

struct Node;

namespace explorer {

/// node->explorer adapter interface
struct IAdapter {
    using Ptr = std::unique_ptr<IAdapter>;

    virtual ~IAdapter() = default;

    /// Returns body for /status request
    virtual bool get_status(io::SerializedMsg& out) = 0;

    virtual bool get_block(io::SerializedMsg& out, uint64_t height) = 0;

    virtual bool get_block_by_hash(io::SerializedMsg& out, const ByteBuffer& hash) = 0;

    virtual bool get_block_by_kernel(io::SerializedMsg& out, const ByteBuffer& key) = 0;

    virtual bool get_blocks(io::SerializedMsg& out, uint64_t startHeight, uint64_t n) = 0;

    virtual bool get_peers(io::SerializedMsg& out) = 0;
};

IAdapter::Ptr create_adapter(Node& node);

}} //namespaces
