#include "../asyncevent.h"
#include <future>
#include <unistd.h>
#include <iostream>

using namespace io;
using namespace std;

// TODO use catch2 TF
// TODO use spdlog

void asyncevent_test() {
    Reactor::Ptr reactor = Reactor::create();

    shared_ptr<AsyncEvent> e = make_shared<AsyncEvent>(
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
                usleep(300000);
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

