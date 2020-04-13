#pragma once

#include <array>

namespace beam
{
    using Dictionary = std::array<const char*, 2048>;

    namespace language
    {
        extern const Dictionary en;
        extern const Dictionary es;
        extern const Dictionary ja;
        extern const Dictionary it;
        extern const Dictionary fr;
        extern const Dictionary cs;
        extern const Dictionary ru;
        extern const Dictionary uk;
        extern const Dictionary zh_Hans;
        extern const Dictionary zh_Hant;
    }
}
