#include "utility/io/timer.h"
#include <iostream>

using namespace beam::io;
using namespace std;


void timer_test() {
    Reactor::Ptr reactor = Reactor::create();
    Timer::Ptr timer = Timer::create(reactor);
    int countdown = 5;

    cout << "setting up one-shot timer" << endl;
    timer->start(
        600,
        false,
        [&reactor, &countdown, &timer] {
            cout << "starting periodic timer" << endl;
            timer->start(
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


