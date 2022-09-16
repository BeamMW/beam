// Copyright 2022 The Beam Team
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

#include <string>
#include <vector>
#ifdef BEAM_ASSET_SWAP_SUPPORT
#include "wallet/client/extensions/dex_board/dex_order.h"
#endif  // BEAM_ASSET_SWAP_SUPPORT

namespace beam::wallet
{

#ifdef BEAM_ASSET_SWAP_SUPPORT
    #define V7_2_ASSETS_SWAP_METHODS(macro) \
        macro(AssetsSwapOffersList, "assets_swap_offers_list", API_READ_ACCESS,  API_SYNC, APPS_BLOCKED) \
        macro(AssetsSwapCreate,     "assets_swap_create",      API_WRITE_ACCESS, API_SYNC, APPS_BLOCKED)
#else  // !BEAM_ASSET_SWAP_SUPPORT
    #define V7_2_ASSETS_SWAP_METHODS(macro)
#endif  // BEAM_ASSET_SWAP_SUPPORT

#define V7_2_API_METHODS(macro) \
    V7_2_ASSETS_SWAP_METHODS(macro)

#ifdef BEAM_ASSET_SWAP_SUPPORT
    struct AssetsSwapOffersList
    {
        struct Response
        {
            std::vector<DexOrder> orders;
        };
    };

    struct AssetsSwapCreate
    {
        Amount           sendAmount;
        beam::Asset::ID  sendAsset = 0;
        Amount           receiveAmount;
        beam::Asset::ID  receiveAsset = 0;
        uint32_t         expireMinutes = 0;
        std::string      comment;

        struct Response
        {
            DexOrder order;
        };
    };
#endif  // BEAM_ASSET_SWAP_SUPPORT
}
