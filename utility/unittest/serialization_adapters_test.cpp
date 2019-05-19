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

#include "core/common.h"
#include "core/serialization_adapters.h"

int main()
{
    // test for the ECC::Point adapter
    {
        ECC::Point in;
        in.m_X.m_pData[0] = 123;
        in.m_Y = 1;

        beam::Serializer ser;
        ser & in;

        auto [buf, size] = ser.buffer();

        beam::Deserializer des;
        des.reset(buf, size);

        ECC::Point out;
        des & out;

        assert(in.m_X == out.m_X);
        assert(in.m_Y == out.m_Y);
    }

    // and etc...

    return 0;
}
