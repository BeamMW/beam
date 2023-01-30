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
#include "hid_key_keeper.h"


namespace beam::wallet
{
	namespace LedgerFw
	{
		struct AppData
		{

			std::string m_sName;
			std::string m_sAppVer;
			ByteBuffer m_Icon;
			ByteBuffer m_KeyPath;

			uint32_t m_BootAddr = 0;
			uint32_t m_SizeNVRam = 0;
			uint32_t m_TargetID = 0;
			uint16_t m_HidProductID = 0;
			std::string m_sTargetVer;

			typedef std::map<uint32_t, ByteBuffer> ZoneMap;
			ZoneMap m_Zones;

			static const char s_szSig[];

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_sName
					& m_sAppVer
					& m_Icon
					& m_KeyPath
					& m_BootAddr
					& m_SizeNVRam
					& m_TargetID
					& m_HidProductID
					& m_sTargetVer
					& m_Zones;
			}


			static bool FindAddr(uint32_t& ret, const char* szLine, const char* szPattern);
			static void HexReadStrict(uint8_t* pDst, const char* sz, uint32_t nBytes);
			static uint32_t Bytes2Addr(const uint8_t* p, uint32_t n);

			void ParseHex(const char* szPath);
			void ParseMap(const char* szPath);

			void Create(const char* szDir);
			void SetIconFromStr(const char* sz, uint32_t nLen);
			void SetBeam();

			void SetTargetNanoS();
			void SetTargetNanoSPlus();
		};

	} // namespace LedgerFw
} // namespace beam::wallet
