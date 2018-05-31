#include "rnd_gen.h"

namespace beam {

RandomGen::RandomGen() {
    std::random_device _rd;
    _rdGen.seed(_rd());
}

} //namespace
