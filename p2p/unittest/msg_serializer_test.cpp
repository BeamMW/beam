#include "p2p/msg_serializer.h"
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

int main() {
    fragment_writer_test();
}
