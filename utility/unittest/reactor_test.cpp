#include "utility/io/reactor.h"
#include <future>
#include <unistd.h>
#include <iostream>

using namespace beam::io;
using namespace std;

// TODO use catch2 TF
// TODO use spdlog

void reactor_start_stop() {
    Config config;
    Reactor::Ptr reactor = Reactor::create(config);

    auto f = std::async(
        std::launch::async,
        [reactor]() {
            usleep(300000);
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
    reactor_start_stop();
}
