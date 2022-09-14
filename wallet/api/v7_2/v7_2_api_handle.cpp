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
#include "v7_2_api.h"
#include "version.h"
#include "bvm/bvm2.h"

namespace beam::wallet
{
#ifdef BEAM_ASSET_SWAP_SUPPORT
    void V72Api::onHandleAssetsSwapOffersList(const JsonRpcId& id, AssetsSwapOffersList&& req)
    {
        AssetsSwapOffersList::Response resp;
        resp.orders = getDexBoard()->getDexOrders();

        doResponse(id, resp);
    }
#endif  // BEAM_ASSET_SWAP_SUPPORT
}
