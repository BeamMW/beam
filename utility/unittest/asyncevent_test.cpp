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

#include "utility/io/asyncevent.h"
#include <future>
#include <iostream>

using namespace beam::io;
using namespace std;

void asyncevent_test() {
    Reactor::Ptr reactor = Reactor::create();

    AsyncEvent::Ptr e = AsyncEvent::create(
        *reactor,
        []() {
            cout << "event triggered in reactor thread" << endl;
        }
    );

    auto f = std::async(
        std::launch::async,
        [reactor, e]() {
            for (int i=0; i<4; ++i) {
                cout << "triggering async event from foreign thread..." << endl;
                e->post();
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

