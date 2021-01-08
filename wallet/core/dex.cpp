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
#include "dex.h"
#include "boost/uuid/random_generator.hpp"
#include "utility/hex.h"

namespace beam::wallet {
    std::string DexOrderID::to_string() const
    {
        return to_hex(data(), size());
    }

    DexOrderID DexOrderID::generate()
    {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        DexOrderID orderId {};
        std::copy(uuid.begin(), uuid.end(), orderId.begin());
        return orderId;
    }

    bool DexOrderID::FromHex(const std::string &hex)
    {
        bool allOK = true;
        const auto vec = from_hex(hex, &allOK);

        if (!allOK || vec.size() != size())
        {
            return false;
        }

        std::copy_n(vec.begin(), size(), begin());
        return true;
    }
}
