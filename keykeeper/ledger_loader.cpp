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

#include "ledger_loader.h"
#include "utility/byteorder.h"

namespace beam::wallet {
namespace LedgerFw {

const char AppData::s_szSig[] = "beam.ledger.fw.1";

void AppData::Create(const char* szDir)
{
	std::string s(szDir);
#ifdef WIN32
	s += '\\';
#else
	s += '/';
#endif

	ParseMap((s + "app.map").c_str());
	ParseHex((s + "app.hex").c_str());

	Serializer ser;
	ser& (*this);

	std::ofstream fs;
	fs.open(s + "beam-ledger.bin", std::ios_base::out);
	if (fs.fail())
		std::ThrowLastError();

	fs.write(s_szSig, sizeof(s_szSig) - 1);
	fs.write(ser.buffer().first, ser.buffer().second);
}


void AppData::HexReadStrict(uint8_t* pDst, const char* sz, uint32_t nBytes)
{
	uint32_t nLen = nBytes << 1;
	if (uintBigImpl::_Scan(pDst, sz, nLen) != nLen)
		Exc::Fail("hex read");
}

uint32_t AppData::Bytes2Addr(const uint8_t* p, uint32_t n)
{
	if (n > sizeof(uint32_t))
		Exc::Fail("addr too wide");
	uint32_t ret = 0;
	memcpy(reinterpret_cast<uint8_t*>(&ret) + sizeof(uint32_t) - n, p, n);
	return ByteOrder::from_be(ret);
}

void AppData::ParseHex(const char* szPath)
{
	ZoneMap::iterator itZ = m_Zones.end();

	std::ifstream fs;
	fs.open(szPath, std::ios_base::in);
	if (fs.fail())
		std::ThrowLastError();

	while (true)
	{
		char sz[0x100];
		fs.getline(sz, _countof(sz));
		if (fs.eof())
			break;
		if (fs.fail())
			std::ThrowLastError();

		if (sz[0] != ':')
			Exc::Fail("unexpected line start");

		uint8_t nBytes, pMid[2], nType;
		HexReadStrict(&nBytes, sz + 1, 1);
		HexReadStrict(pMid, sz + 3, 2);
		HexReadStrict(&nType, sz + 7, 1);

		uint8_t pData[0x100];
		HexReadStrict(pData, sz + 9, nBytes);

		switch (nType)
		{
		case 0: // std entry
			if (m_Zones.end() == itZ)
				Exc::Fail("entry out-of zone");

			if (!nBytes)
				Exc::Fail("entry without bytes");

			{
				auto nOffset = Bytes2Addr(pMid, 2);
				if (itZ->second.size() != nOffset)
					Exc::Fail("zone gap"); // maybe should allow

				itZ->second.insert(itZ->second.end(), pData, pData + nBytes);
			}

			break;

		case 1: // zone end
			itZ = m_Zones.end();
			break;

		case 4: // zone start
		{
			auto nAddr = Bytes2Addr(pData, nBytes);
			itZ = m_Zones.emplace(nAddr << 16, ByteBuffer()).first;
		}
		break;

		case 5: // boot addr
			if (m_BootAddr)
				Exc::Fail("boot addr already set");
			m_BootAddr = Bytes2Addr(pData, nBytes);
			break;

		default:
			Exc::Fail("bad record type");
		}
	}

	if (m_Zones.empty())
		Exc::Fail("no zones");

	// check no overlap, merge adjacent zones
	for (auto it = m_Zones.begin(); ; )
	{
		if (it->second.empty())
			Exc::Fail("empty zone");

		auto itNext = it;
		++itNext;

		if (m_Zones.end() == itNext)
			break;

		if (it->first + it->second.size() > itNext->first)
			Exc::Fail("zone overlap");

		//			if (z.get_End() < z2.m_Key)
		it = itNext;
		//else
		//{
		//	// merge
		//	z.m_Data.insert(z.m_Data.end(), z2.m_Data.begin(), z2.m_Data.end());
		//	m_Zones.Delete(z2);
		//}
	}
}

bool AppData::FindAddr(uint32_t& ret, const char* szLine, const char* szPattern)
{
	if (ret)
		return true; // already set

	if (!strstr(szLine, szPattern))
		return false;

	static const char sz2[] = "0x";
	auto szPtr = strstr(szLine, sz2);
	if (szPtr)
	{
		szPtr += _countof(sz2) - 1; // nanos-style

		uintBigFor<uint64_t>::Type x;
		if (x.Scan(szPtr) != x.nTxtLen)
			return false;

		x.ExportWord<1>(ret);
	}
	else
	{
		// nanosplus-style
		uintBigFor<uint32_t>::Type x;
		if (x.Scan(szLine) != x.nTxtLen)
			return false;

		x.Export(ret);
	}

	return true;
}

void AppData::ParseMap(const char* szPath)
{
	uint32_t n0 = 0, n1 = 0;

	std::ifstream fs;
	fs.open(szPath, std::ios_base::in);
	if (fs.fail())
		std::ThrowLastError();

	while (true)
	{
		if (n0 && n1)
			break;

		char sz[0x100];
		fs.getline(sz, _countof(sz));
		if (fs.eof())
			Exc::Fail("couldn't find nvram size");
		if (fs.fail())
			std::ThrowLastError();


		FindAddr(n0, sz, "_nvram_data");
		FindAddr(n1, sz, "_envram_data");
	}

	if (n0 > n1)
		Exc::Fail("invalid nvram size");

	m_SizeNVRam = n1 - n0;
}

void AppData::SetIconFromStr(const char* sz, uint32_t nLen)
{
	if (1 & nLen)
		Exc::Fail("bad icon");

	uint32_t nB = nLen >> 1;
	if (nB)
	{
		m_Icon.resize(nB);
		if (uintBigImpl::_Scan(&m_Icon.front(), sz, nLen) != nLen)
			Exc::Fail("bad icon");
	}
}

void AppData::SetBeam()
{
	m_sName = "Beam";

	// Our bip44 path is: "44'1533''"
	static const uint8_t pPath[] = { /*secp256k1*/ 0xff,0, /*bip44*/ 0x80,0,0,44, /*beam*/ 0x80,0,5,0xfd };
	m_KeyPath.assign(pPath, pPath + _countof(pPath));
}

void AppData::SetTargetNanoS()
{
	m_HidProductID = 0x1011;
	m_TargetID = 0x31100004;
	m_sTargetVer = "2.1.0";

	static const char szIcon[] = "0100000000ffffff00000080018001400240022004a0059009500a481228142424f42f0240fe7f0000";
	SetIconFromStr(szIcon, sizeof(szIcon) - 1);
}

void AppData::SetTargetNanoSPlus()
{
	m_HidProductID = 0x5011;
	m_TargetID = 0x33100004;
	m_sTargetVer = "1.0.4";

	static const char szIcon[] = "0100000000ffffff00000030001280041002b4804ca014240985223f0940fe1f0000";
	SetIconFromStr(szIcon, sizeof(szIcon) - 1);
}

} // namespace LedgerFw
} // namespace beam::wallet

