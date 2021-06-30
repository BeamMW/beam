// Copyright 2018 The Beam Team
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
#include "v6_1_api.h"

namespace beam::wallet
{
    void V61Api::onSyncProgress(int done, int total)
    {
        if (!_evSubscribed)
        {
            return;
        }

        // THIS METHOD IS NOT GUARDED
        try
        {
            auto walletDB = getWalletDB();

            Block::SystemState::Full tip;
            walletDB->get_History().get_Tip(tip);

            Merkle::Hash tipHash;
            tip.get_Hash(tipHash);

            json msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", "ev_sync_progress"},
                {"result",
                    {
                        {"current_height", tip.m_Height},
                        {"current_state_hash", to_hex(tipHash.m_pData, tipHash.nBytes)},
                        {"current_state_timestamp", tip.m_TimeStamp},
                        {"prev_state_hash", to_hex(tip.m_Prev.m_pData, tip.m_Prev.nBytes)},
                        {"is_in_sync", IsValidTimeStamp(tip.m_TimeStamp)},
                        {"done", done},
                        {"total", total}
                    }
                }
            };

            _handler.sendAPIResponse(msg);
        }
        catch(std::exception& e)
        {
            LOG_ERROR() << "V61Api::onSyncProgress failed: " << e.what();
        }
    }
}
