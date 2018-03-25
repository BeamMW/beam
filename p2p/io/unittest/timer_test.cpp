#include "../timer.h"
#include <unistd.h>
#include <iostream>

using namespace io;
using namespace std;

// TODO use catch2 TF
// TODO use spdlog

void timer_test() {
    Reactor::Ptr reactor = Reactor::create();
    Timer timer(reactor);
    int countdown = 5;

    cout << "setting up one-short timer" << endl;
    timer.start(
        600,
        false,
        [&reactor, &countdown, &timer] {
            cout << "starting periodic timer" << endl;
            timer.start(
                300,
                true,
                [&reactor, &countdown] {
                    cout << countdown << endl;
                    if (--countdown == 0)
                    reactor->stop();
                }
            );
        }
    );

    cout << "Starting" << endl;
    reactor->run();
    cout << "Stopping" << endl;
}

int main() {
    timer_test();
}


