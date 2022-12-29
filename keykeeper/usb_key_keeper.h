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

#include "remote_key_keeper.h"

namespace beam::wallet
{
    struct HidInfo
    {
        struct Entry
        {
            std::string m_sPath;
            std::string m_sManufacturer;
            std::string m_sProduct;

            uint16_t m_Vendor;
            uint16_t m_Product;
            uint16_t m_Version;
        };

        static std::vector<Entry> Enum(uint16_t nVendor);
    };

    struct UsbIO
    {
#ifdef WIN32
        HANDLE m_hFile;
#else // WIN32
        int m_hFile;
#endif // WIN32

        struct Frame;
        struct FrameReader;

        void WriteFrame(const uint8_t*, uint16_t);
        uint16_t ReadFrame(uint8_t*, uint16_t);
        uint16_t Read(void*, uint16_t);

        UsbIO();
        ~UsbIO();

        void Open(const char* szPath); // throws exc on error
    };
}


