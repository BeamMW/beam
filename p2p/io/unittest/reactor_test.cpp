#include "../reactor.h"
#include <future>
#include <unistd.h>

using namespace io;

void reactor_start_stop() {
    Reactor::Ptr reactor = Reactor::create();

    auto future = std::async(
        std::launch::async,
        [reactor]() {
            usleep(300000);
            reactor->stop();
        }
    );

    reactor->run();

    future.get();
}

int main() {
    reactor_start_stop();
}
