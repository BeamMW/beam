#pragma once
#include <memory>

namespace io {

class Reactor : public std::enable_shared_from_this<Reactor> {
public:
    using Ptr = std::shared_ptr<Reactor>;

    static Ptr create();

    struct Object {
        Reactor::Ptr reactor;
        void* handle;
    };
private:
    Reactor();

};

}
