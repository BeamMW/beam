#include "utility/message_queue.h"
#include <future>
#include <iostream>
#include <assert.h>

using namespace std;
using namespace beam;

static const string testStr("some moveble data");
static const io::Config config;

struct Message {
    int n=0;
    unique_ptr<string> d;
};

struct SomeAsyncObject {
    io::Reactor::Ptr reactor;
    std::future<void> f;

    SomeAsyncObject() :
        reactor(io::Reactor::create(config))
    {}

    void run() {
        f = std::async(
            std::launch::async,
            [this]() {
                reactor->run();
            }
        );
    }

    void wait() { f.get(); }
};

struct RXThread : SomeAsyncObject {
    RX<Message> rx;
    std::vector<int> received;

    RXThread() :
        rx(
            reactor,
            [this](Message&& msg) {
                if (msg.n == 0) {
                    reactor->stop();
                    return;
                }
                if (*msg.d != testStr) {
                    received.push_back(0);
                } else {
                    received.push_back(msg.n);
                }
            }
        )
    {}
};

void simplex_channel_test() {
    RXThread remote;
    TX<Message> tx = remote.rx.get_tx();
    std::vector<int> sent;

    remote.run();

    for (int i=1; i<=100500; ++i) {
        tx.send(Message { i, make_unique<string>(testStr) } );
        sent.push_back(i);
    }

    tx.send(Message { 0, make_unique<string>(testStr) } );

    remote.wait();

    assert(remote.received == sent);
}

int main() {
    simplex_channel_test();
}

