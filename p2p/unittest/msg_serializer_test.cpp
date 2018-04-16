#include "p2p/msg_serializer.h"
#include "p2p/msg_deserializer.h"
#include "utility/helpers.h"
#include <iostream>
#include <assert.h>

using namespace beam;
using namespace std;

void fragment_writer_test() {
    std::vector<io::SharedBuffer> fragments;
    size_t totalSize=0;
    FragmentWriter w(79,
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

void msg_serializer_test_1() {
    MsgSerializeOstream os(50);
    MsgSerializer ser(os);

    os.new_message(protocol::MsgType::ping);

    std::vector<int> ooo;
    for (int i=0; i<123; ++i) ooo.push_back(i);

    ser & 3 & 0xFFFFFFFF & ooo;

    std::vector<io::SharedBuffer> fragments;

    os.finalize(fragments);

    for (size_t j=0, sz=fragments.size(); j<sz; ++j)
        cout << "(" << j << ") " << to_hex(fragments[j].data, fragments[j].size) << "\n";

    assert(!fragments.empty() && fragments[0].size >= protocol::MsgHeader::SIZE);

    protocol::MsgHeader header;
    header.read(fragments[0].data);
    bool ok = header.verify();
    assert(ok && "read header");
    assert(header.type == protocol::MsgType::ping);

    // deserialize buffer is contiguous, must be growing on p2p side
    void* buffer = alloca(header.size);
    uint8_t* p = (uint8_t*)buffer;
    size_t bytes = fragments[0].size - protocol::MsgHeader::SIZE;
    memcpy(p, fragments[0].data + protocol::MsgHeader::SIZE, bytes);
    for (size_t j=1, sz=fragments.size(); j<sz; ++j) {
        memcpy(p + bytes, fragments[j].data, fragments[j].size);
        bytes += fragments[j].size;
    }
    assert(bytes == header.size);

    MsgDeserializer des;
    des.reset(buffer, bytes);

    int i=0;
    uint32_t x=0;
    std::vector<int> v;
    des & i & x & v;
    assert(i == 3);
    assert(x == 0xFFFFFFFF);
    assert(v == ooo);

    // now it must reset correctly
    i=666; x=333;
    des.reset(buffer, bytes);
    des & i & x & v;
    assert(i == 3);
    assert(x == 0xFFFFFFFF);
    assert(v == ooo);

    // now it must throw
    try {
        des.reset(buffer, bytes-1);
        des & i & x & v;
        assert(false && "must have been thrown here");
    } catch (...) {}
}

struct SomeObject {
    int i=0;
    size_t x=0;
    std::vector<int> ooo;

    bool operator==(const SomeObject& o) const { return i==o.i && x==o.x && ooo==o.ooo; }

    SERIALIZE(i,x,ooo);
};

void msg_serializer_test_2() {
    MsgSerializeOstream os(50);
    MsgSerializer ser(os);

    os.new_message(protocol::MsgType::ping);

    SomeObject msg;
    msg.i = 3;
    msg.x = 0xFFFFFFFF;
    for (int i=0; i<123; ++i) msg.ooo.push_back(i);

    ser & msg;

    std::vector<io::SharedBuffer> fragments;
    os.finalize(fragments);

    assert(!fragments.empty() && fragments[0].size >= protocol::MsgHeader::SIZE);

    SomeObject deserialized;
    MsgDeserializer des;
    int errorCode=0;

    MsgReader reader(
        12,
        [&des, &deserialized, &errorCode]
        (MsgReader::Error ec, protocol::MsgType type, const void* data, size_t size) -> bool
        {
            errorCode = ec;
            if (ec == MsgReader::no_error) {
                assert(type == protocol::MsgType::ping);
                des.reset(data, size);
                des.deserialize(deserialized);
                return true;
            }
            return false;
        }
    );

    for (const auto& f: fragments) {
        reader.new_data_from_stream(f.data, f.size);
    }

    assert(msg == deserialized);
}

int main() {
    fragment_writer_test();
    msg_serializer_test_1();
    msg_serializer_test_2();
}
