#include "utility/io/asyncevent.h"
#include <future>
#include <iostream>

using namespace beam::io;
using namespace std;

void asyncevent_test() {
    Reactor::Ptr reactor = Reactor::create();

    AsyncEvent::Ptr e = AsyncEvent::create(
        reactor,
        []() {
            cout << "event triggered in reactor thread" << endl;
        }
    );

    auto f = std::async(
        std::launch::async,
        [reactor, e]() {
            for (int i=0; i<4; ++i) {
                cout << "triggering async event from foreign thread..." << endl;
                e->trigger();
                this_thread::sleep_for(chrono::microseconds(300000));
                //usleep(300000);
            }
            cout << "stopping reactor from foreign thread..." << endl;
            reactor->stop();
        }
    );

    cout << "starting reactor..." << endl;
    reactor->run();
    cout << "reactor stopped" << endl;

    f.get();
}

int main() {
    asyncevent_test();
}

