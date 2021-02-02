//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include "api_errors.h"
#include "wallet/transactions/swaps/common.h"

namespace beam::wallet
{
    class swap_coin_fail: public jsonrpc_exception
    {
    public:
        explicit swap_coin_fail(AtomicSwapCoin coin)
            : jsonrpc_exception(ApiError::SwapFailToConnect,
                                std::string("There is not connection with the") + std::to_string(coin) + " wallet")
        {
        }
    };

    class swap_no_beam: public jsonrpc_exception
    {
    public:
        swap_no_beam()
            : jsonrpc_exception(ApiError::SwapNotEnoughtBeams)
        {
        }
    };
}
