// Copyright 2018-2021 The Beam Team
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

#include <iostream>
#include <iomanip>

#include "../bvm2_impl.h"
using namespace beam;
using namespace beam::bvm2;

void LoadCode(ByteBuffer& res, const char* sz)
{
    std::FStream fs;
    fs.Open(sz, true, true);

    res.resize(static_cast<size_t>(fs.get_Remaining()));
    if (!res.empty())
        fs.read(&res.front(), res.size());

    Processor::Compile(res, res, Processor::Kind::Contract);
}

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Please, specify WASM file" << std::endl;
        return -1;
    }
    try
    {
        ByteBuffer buffer;
        LoadCode(buffer, argv[1]);
        ShaderID sid;
        get_ShaderID(sid, Blob(buffer));
        //std::cout << "#pragma once\n";
        std::cout << "// SID: " << sid.str() << '\n';
        std::cout << "static const ShaderID s_SID = {";
        auto c = sizeof(ShaderID);
        unsigned int i = 0;
        std::cout << std::hex << std::setfill('0') << std::setw(2);
        for (; i < c - 1; ++i)
        {
            std::cout << "0x" << std::setw(2) << static_cast<int>(sid.m_pData[i]) << ",";
        }
        std::cout << "0x" << std::setw(2) << static_cast<int>(sid.m_pData[i]) << "};";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Failed to calc SID: " << ex.what() << std::endl;
    }
    return -1;
}