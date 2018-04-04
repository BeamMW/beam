#pragma once

#include "core/common.h"

namespace beam
{
    struct NodeConfig
    {
        int port;
    };

    struct Node
    {
        void listen(const NodeConfig& config);
    };
}