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
#pragma once

#include <string>
#include <array>

namespace beam::wallet
{
    struct DexOrderID: std::array<uint8_t, 16>
    {
        [[nodiscard]] std::string to_string() const;
        static DexOrderID generate();

        DexOrderID() = default;
        bool FromHex(const std::string& hex);

        template <typename Archive>
        void serialize(Archive& ar)
        {
            auto& arr = *static_cast<std::array<uint8_t, 16>*>(this);
            ar & arr;
        }
    };
}

