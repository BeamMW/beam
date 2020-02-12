// Copyright 2019 The Beam Team
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

#include "swap_offer.h"

namespace beam::wallet
{
    void SwapOffer::SetTxParameters(const PackedTxParameters& parameters)
    {
        // Do not forget to set other SwapOffer members also!
        SubTxID subTxID = kDefaultSubTxID;
        Deserializer d;
        for (const auto& p : parameters)
        {
            if (p.first == TxParameterID::SubTxIndex)
            {
                // change subTxID
                d.reset(p.second.data(), p.second.size());
                d & subTxID;
                continue;
            }

            SetParameter(p.first, p.second, subTxID);
        }
    }
} // namespace beam::wallet
