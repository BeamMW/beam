////////////////////////
#include "../../common.h"

static const uint8_t s_pCocoon[] = {
#include "test_v0.txt"
};

namespace Upgradable3
{
    void OnUpgraded_From2()
    {
    }
}

#include "../cocoon_v2_impl.h"
