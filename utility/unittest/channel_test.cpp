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

#include "utility/message_queue.h"
#include <future>
#include <iostream>
#include <assert.h>

using namespace std;
using namespace beam;

static const string testStr("some moveble data");

struct Message {
    int n=0;
    unique_ptr<string> d;
};

struct SomeAsyncObject {
    io::Reactor::Ptr reactor;
    std::future<void> f;

    SomeAsyncObject() :
        reactor(io::Reactor::create())
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
            *reactor,
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

