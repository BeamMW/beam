#pragma once

#include "array"

namespace beam
{
    typedef std::array<const char*, 2048> Dictionary;

    namespace language
    {
        extern const Dictionary en;
    }
}