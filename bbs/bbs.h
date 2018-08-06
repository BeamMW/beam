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

#pragma once
#include "p2p/protocol_base.h"
#include "utility/io/address.h"
#include <map>

namespace beam { namespace bbs {

struct Config {
    io::Address serverAddress;
    uint8_t spectre=0;
    uint32_t historyDepth=0;

    SERIALIZE(serverAddress, spectre, historyDepth);
};

// address->spectre
using Servers = std::map<io::Address, uint8_t>;

// encrypted message
using Bytes = io::SharedBuffer;

struct Message {
    // sequence # local to the server
    uint32_t seqNumber=0;

    // timestamp in seconds since the epoch
    uint32_t timestamp=0;

    // encrypted message
    Bytes bytes;

    SERIALIZE(seqNumber, timestamp, bytes);
};

struct Request {
    enum Action { subscribe, unsubscribe, get_servers };

    Action action=subscribe;

    // seconds from now to the past, just subscribe for future messages if ==0
    uint32_t startTimeDepth=0;

    // seconds from now to the past
    uint32_t endTimeDepth=0;

    SERIALIZE(action, startTimeDepth, endTimeDepth);
};

const MsgType CONFIG_MSG_TYPE = 1;
const MsgType SERVERS_MSG_TYPE = 2;
const MsgType MESSAGE_MSG_TYPE = 3;
const MsgType REQUEST_MSG_TYPE = 4;
const MsgType PUBLISH_MSG_TYPE = 5;


}} //namespaces
