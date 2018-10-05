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

#include "p2p/msg_serializer.h"
#include "p2p/msg_reader.h"
#include "p2p/protocol.h"
#include "utility/helpers.h"
#include <iostream>
#include <assert.h>

using namespace beam;
using namespace std;

void fragment_writer_test() {
    std::vector<io::SharedBuffer> fragments;
    size_t totalSize=0;
    io::FragmentWriter w(79, 11,
        [&fragments,&totalSize](io::SharedBuffer&& f) {
            totalSize += f.size;
            cout << "OnNewFragment(" << f.size << " of " << totalSize << ")\n";
            fragments.push_back(std::move(f));
        }
    );

    const char str[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    for (int i=0; i<10; ++i) {
        w.write(str, 12);
        w.write(str + 12, 33);
        w.write(str + 45, 19);
        w.finalize();
        cout << "finalized\n";
        char* buf = (char*)alloca(totalSize);
        char* p = buf;
        for (const auto& f : fragments) {
            memcpy(p, f.data, f.size);
            p += f.size;
        }
        assert(strlen(str) == totalSize);
        assert(memcmp(buf, str, totalSize) == 0);
        fragments.clear();
        totalSize = 0;
    }
}

using IntList = std::vector<int>;

struct SomeObject {
    int i=0;
    size_t x=0;
    std::vector<int> ooo;

    bool operator==(const SomeObject& o) const { return i==o.i && x==o.x && ooo==o.ooo; }

    SERIALIZE(i,x,ooo);
};

struct MsgHandler : IErrorHandler {
    void on_protocol_error(uint64_t fromStream, ProtocolError error) override {
        cout << __FUNCTION__ << "(" << fromStream << "," << error << ")" << endl;
    }

    void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) override {
        cout << __FUNCTION__ << "(" << fromStream << "," << errorCode << ")" << endl;
    }

    bool on_ints(uint64_t fromStream, IntList&& msg) {
        cout << __FUNCTION__ << "(" << fromStream << "," << msg.size() << ")" << endl;
        receivedInts = msg;
        return true;
    }

    bool on_some_object(uint64_t fromStream, SomeObject&& msg) {
        cout << __FUNCTION__ << "(" << fromStream << "," << msg.i << ")" << endl;
        receivedObj = msg;
        return true;
    }

    IntList receivedInts;
    SomeObject receivedObj;
};

void msg_serializer_test_1() {
    MsgType type = 99;

    MsgHandler handler;
    Protocol protocol(0xBE, 0xA6, 0x66, 256, handler, 50);
    protocol.add_message_handler<MsgHandler, IntList, &MsgHandler::on_ints>(type, &handler, 8, 1<<24);

    MsgSerializer ser(50, protocol.get_default_header());

    ser.new_message(type);

    IntList ooo;
    for (int i=0; i<123; ++i) ooo.push_back(i);

    ser & ooo;

    std::vector<io::SharedBuffer> fragments;

    ser.finalize(fragments);

    for (size_t j=0, sz=fragments.size(); j<sz; ++j)
        cout << "(" << j << ") " << to_hex(fragments[j].data, fragments[j].size) << "\n";

    assert(!fragments.empty() && fragments[0].size >= MsgHeader::SIZE);

    MsgHeader header(fragments[0].data);
    assert(protocol.approve_msg_header(1, header));
    assert(header.type == type);

    // deserialize buffer is contiguous, must be growing on p2p side
    void* buffer = alloca(header.size);
    uint8_t* p = (uint8_t*)buffer;
    size_t bytes = fragments[0].size - MsgHeader::SIZE;
    memcpy(p, fragments[0].data + MsgHeader::SIZE, bytes);
    for (size_t j=1, sz=fragments.size(); j<sz; ++j) {
        memcpy(p + bytes, fragments[j].data, fragments[j].size);
        bytes += fragments[j].size;
    }
    assert(bytes == header.size);

    Deserializer des;
    des.reset(buffer, bytes);

    std::vector<int> v;
    des & v;
    assert(v == ooo);

    // now it must reset correctly
    des.reset(buffer, bytes);
    des & v;
    assert(v == ooo);

    // now it must throw
    try {
        des.reset(buffer, bytes-1);
        des & v;
        assert(false && "must have been thrown here");
    } catch (...) {}
}



void msg_serializer_test_2() {
    MsgType type = 222;

    MsgHandler handler;
    Protocol protocol(0xAA, 0xBB, 0xCC, 256, handler, 50);

    protocol.add_message_handler<MsgHandler, SomeObject, &MsgHandler::on_some_object>(type, &handler, 8, 1<<24);

    SomeObject msg;
    msg.i = 3;
    msg.x = 0xFFFFFFFF;
    for (int i=0; i<123; ++i) msg.ooo.push_back(i);

    std::vector<io::SharedBuffer> fragments;
    protocol.serialize(fragments, type, msg);

    assert(!fragments.empty() && fragments[0].size >= MsgHeader::SIZE);

    MsgReader reader(
        protocol,
        123456,
        12
    );

    for (const auto& f: fragments) {
        reader.new_data_from_stream(io::EC_OK, f.data, f.size);
    }

    assert(msg == handler.receivedObj);
}

int main() {
    fragment_writer_test();
    msg_serializer_test_1();
    msg_serializer_test_2();
}
