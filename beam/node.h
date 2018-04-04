#pragma once

#include "core/common.h"

namespace beam
{

    struct Node
    {
        struct Config
        {
            int port;
        };

        void listen(const Config& config);
    };
}