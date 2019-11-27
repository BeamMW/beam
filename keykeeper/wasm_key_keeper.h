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

#pragma once

#include "core/block_rw.h"
#include "3rdparty/utilstrencodings.h"

namespace beam::wallet
{
    template <typename T>
    std::string to_base64(const T& obj)
    {
        ByteBuffer buffer;
        {
            Serializer s;
            s & obj;
            s.swap_buf(buffer);
        }

        return EncodeBase64(buffer.data(), buffer.size());
    }

    template <>
    std::string to_base64(const KernelParameters& obj)
    {
        ByteBuffer buffer;
        {
            Serializer s;
            s   
                & obj.height.m_Min
                & obj.height.m_Max
                & obj.fee
                & obj.commitment
                & obj.lockImage
                & obj.hashLock;
            s.swap_buf(buffer);
        }

        return EncodeBase64(buffer.data(), buffer.size());
    }

    template <typename T>
    T from_base64(const std::string& base64)
    {
        T obj;
        {
            auto data = DecodeBase64(base64.data());

            Deserializer d;
            d.reset(data.data(), data.size());

            d & obj;
        }

        return obj;
    }

    template <>
    KernelParameters from_base64(const std::string& base64)
    {
        KernelParameters obj;
        {
            auto data = DecodeBase64(base64.data());

            Deserializer d;
            d.reset(data.data(), data.size());

            d   
                & obj.height.m_Min
                & obj.height.m_Max
                & obj.fee
                & obj.commitment
                & obj.lockImage
                & obj.hashLock;
        }

        return obj;
    }
};
