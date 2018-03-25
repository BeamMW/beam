#include "reactor.h"

namespace io {

Reactor::Ptr Reactor::create() {
    return Reactor::Ptr(new Reactor());
}

Reactor::Reactor() {
}

} //namespace
