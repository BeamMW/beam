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

///////////////////////////
// AppData
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
	ser & (*this);

	std::ofstream fs;
	fs.open(s + "beam-ledger.bin", std::ios_base::out);
	if (fs.fail())
		std::ThrowLastError();

	fs.write(s_szSig, sizeof(s_szSig) - 1);
	fs.write(ser.buffer().first, ser.buffer().second);
}

void AppData::Load(const char* szPath)
{
	Exc::CheckpointTxt cp1("Ledger data load");

	ByteBuffer buf;

	{
		std::FStream fs;
		fs.Open(szPath, true, true);

		char szSig[sizeof(s_szSig) - 1];
		fs.read(szSig, sizeof(szSig));
		if (memcmp(szSig, s_szSig, sizeof(szSig)))
			Exc::Fail("sig mismatch");

		buf.resize((size_t) fs.get_Remaining());
		if (!buf.empty())
			fs.read(&buf.front(), buf.size());
	}	


	Deserializer der;
	der.reset(buf);

	der & (*this);
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

///////////////////////////
// Loader
#pragma pack (push, 1)
struct Loader::Cmd
{
	uint8_t cla = 0xe0;
	uint8_t ins = 0;
	uint8_t p1 = 0;
	uint8_t p2 = 0;

	Cmd(uint8_t ins_ = 0, uint8_t p1_ = 0)
	{
		ins = ins_;
		p1 = p1_;
	}
};
#pragma pack (pop)

void Loader::CbcCoder::Init(const ECC::Hash::Value& hvSecret, uint32_t iKey)
{
	ECC::Scalar::Native sk;

	iKey = ByteOrder::to_be(iKey);

	for (uint32_t i = 0; ; i++)
	{
		ECC::NoLeak<ECC::Scalar> s_;
		ECC::Hash::Processor hp;
		hp.Write(&iKey, sizeof(iKey));

		hp
			<< i
			<< hvSecret
			>> s_.V.m_Value;

		if (sk.ImportNnz(s_.V))
			break;
	}

	ECC::Point::Native ptN = ECC::Context::get().G * sk;
	ECC::Point::Storage ptS;
	ptN.Export(ptS);

	ECC::Hash::Processor()
		<< (uint8_t)4u
		<< ptS.m_X
		<< ptS.m_Y
		>> ptS.m_X;

	static_assert(ptS.m_X.nBytes >= sizeof(m_pIv));
	m_Aes.Init128(ptS.m_X.m_pData);
	ZeroObject(m_pIv);

}

void Loader::CbcCoder::Encode(uint8_t* pDst, const uint8_t* pSrc, uint32_t len)
{
	assert(!(len % sizeof(m_pIv)));

	for (uint32_t i = 0; i < len; i += sizeof(m_pIv))
	{
		memxor(m_pIv, pSrc, sizeof(m_pIv));
		pSrc += sizeof(m_pIv);

		m_Aes.EncodeBlock(m_pIv, m_pIv);
		if (pDst)
		{
			memcpy(pDst, m_pIv, sizeof(m_pIv));
			pDst += sizeof(m_pIv);
		}
	}
}

void Loader::CbcCoder::Decode(uint8_t* pDst, const uint8_t* pSrc, uint32_t len)
{
	assert(!(len % sizeof(m_pIv)));

	for (uint32_t i = 0; i < len; i += sizeof(m_pIv))
	{
		uint8_t pDec[sizeof(m_pIv)];
		m_Aes.DecodeBlock(pDec, pSrc);

		memxor(pDec, m_pIv, sizeof(m_pIv));
		memcpy(m_pIv, pSrc, sizeof(m_pIv));
		pSrc += sizeof(m_pIv);

		if (pDst)
		{
			memcpy(pDst, pDec, sizeof(m_pIv));
			pDst += sizeof(m_pIv);
		}
	}
}


void Loader::DataOut(const void* p, uint8_t n) {
	memcpy(m_pData + m_Data, p, n);
	m_Data += n;
}

template <typename T>
void Loader::DataOutBlob(const T& x) {
	DataOut(&x, sizeof(x));
}

template <typename T>
void Loader::DataOut_be(T x) {
	x = ByteOrder::to_be(x);
	DataOutBlob(x);
}

uint8_t* Loader::DataIn(uint8_t n)
{
	if (m_Data - m_Read < n)
		Exc::Fail("not enough data");

	auto pRet = m_pData + m_Read;
	m_Read += n;
	return pRet;
}

template <typename T>
void Loader::DataInBlob(T& x) {
	memcpy(&x, DataIn(sizeof(x)), sizeof(x));
}

template <typename T>
T Loader::DataIn_be() {
	T x;
	DataInBlob(x);
	return ByteOrder::from_be(x);
}

uint16_t Loader::Exchange(const Cmd& cmd)
{
	uint16_t nFrame = sizeof(Cmd) + 1 + m_Data;
	assert(nFrame < sizeof(m_pData));
	uint8_t pFrame[0x100];
	memcpy(pFrame, &cmd, sizeof(cmd));
	pFrame[sizeof(cmd)] = m_Data;
	memcpy(pFrame + sizeof(cmd) + 1, m_pData, m_Data);

	m_Io.WriteFrame(pFrame, nFrame);

	nFrame = m_Io.ReadFrame(m_pData, sizeof(m_pData));
	if (nFrame > sizeof(m_pData))
		Exc::Fail("res too large");
	if (nFrame < sizeof(uint16_t))
		Exc::Fail("res too short");

	m_Read = 0;
	m_Data = (uint8_t) (nFrame - sizeof(uint16_t));
	memcpy(&nFrame, m_pData + m_Data, sizeof(uint16_t));

	return ByteOrder::from_be(nFrame);
}

uint16_t Loader::ExchangeSec(const Cmd& cmd)
{
	const uint8_t nSizeAes = 16;
	const uint8_t nSizeMac = 14;
	const uint8_t nPadChar = 0x80;

	m_pData[m_Data++] = nPadChar;

	uint8_t nPad = 0xf - (0xf & (0xf + m_Data));
	memset0(m_pData + m_Data, nPad);
	m_Data += nPad;

	m_Enc.Encode(m_pData, m_pData, m_Data);
	m_Mac.Encode(nullptr, m_pData, m_Data);
	memcpy(m_pData + m_Data, m_Mac.m_pIv + nSizeAes - nSizeMac, nSizeMac);
	m_Data += nSizeMac;

	uint16_t retCode = Exchange(cmd);

	if (m_Data > 0)
	{
		if (m_Data < nSizeMac)
			Exc::Fail("no mac");
		m_Data -= nSizeMac;

		if (0xf & m_Data)
			Exc::Fail("bad dec size");

		m_Mac.Encode(nullptr, m_pData, m_Data);
		if (memcmp(m_Mac.m_pIv + nSizeAes - nSizeMac, m_pData + m_Data, nSizeMac))
			Exc::Fail("bad mac");

		m_Enc.Decode(m_pData, m_pData, m_Data);
		while (true)
		{
			if (!m_Data)
				Exc::Fail("no dec pad");
			if (m_pData[--m_Data] == nPadChar)
				break;
		}
	}

	return retCode;
}

void Loader::TestStatus(uint16_t res)
{
	if (0x9000 != res)
	{
		std::ostringstream ss;
		ss << "Hid status " << uintBigFor<uint16_t>::Type(res);

		Exc::Fail(ss.str().c_str());
	}
}

//void Loader::TestSize(uint16_t n)
//{
//	if (m_Data != n)
//	{
//		std::ostringstream ss;
//		ss << "Read size Expected=" << n << ", Actual=" << (uint16_t) m_Data;
//		Exc::Fail(ss.str().c_str());
//	}
//}

template <typename T>
void Loader::TestInVal(const T& nExp, const T& nActial)
{
	if (nExp != nActial)
	{
		std::ostringstream ss;
		ss << "Expected=" << nExp << ", Actual=" << nActial;
		Exc::Fail(ss.str().c_str());
	}
}

void Loader::Ecdsa::Sign(const ECC::Scalar::Native& sk, const ECC::Hash::Value& msg)
{
	ECC::Scalar::Native kNonce;
	{
		ECC::NonceGenerator nonceGen("beam-ecdsa");

		ECC::NoLeak<ECC::Scalar> s_;
		s_.V = sk;
		nonceGen << msg;

		nonceGen >> kNonce;
	}

	ECC::Point::Native ptN = ECC::Context::get().G * kNonce;
	ECC::Point pt(ptN);
	m_r.m_Value = pt.m_X; // don't care about overflow

	ECC::Scalar::Native kSig = ECC::Scalar::Native(m_r) * sk;
	kSig += Cast::Reinterpret<ECC::Scalar>(msg);

	kNonce.SetInv(kNonce);
	kSig *= kNonce;
	m_s = kSig;
}

bool Loader::Ecdsa::IsValid(const ECC::Point::Native& pubKey, const ECC::Hash::Value& msg) const
{
	if ((m_r.m_Value == Zero) || (m_s.m_Value == Zero))
		return false;

	ECC::Mode::Scope scope(ECC::Mode::Fast);

	ECC::Scalar::Native s1 = m_s;
	s1.SetInv(s1);

	ECC::Scalar::Native k = s1 * Cast::Reinterpret<ECC::Scalar>(msg);
	ECC::Point::Native ptN = ECC::Context::get().G * k;

	k = s1 * Cast::Reinterpret<ECC::Scalar>(m_r);
	ptN += pubKey * k;

	ECC::Point pt(ptN);
	return pt.m_X == m_r.m_Value;
}

void Loader::DataOutSig(const Ecdsa& x)
{
	uint8_t nPadR = x.m_r.m_Value.get_Msb();
	uint8_t nPadS = x.m_s.m_Value.get_Msb();
	uint8_t nLen = sizeof(x) + nPadR + nPadS; // 64-66 bytes

	DataOut_be<uint8_t>(nLen + 6);
	DataOut_be<uint8_t>(0x30);
	DataOut_be<uint8_t>(nLen + 4);
	DataOut_be<uint8_t>(2);
	DataOut_be<uint8_t>(sizeof(x.m_r.m_Value) + nPadR);
	if (nPadR)
		DataOut_be<uint8_t>(0);
	DataOutBlob(x.m_r);
	DataOut_be<uint8_t>(2);
	DataOut_be<uint8_t>(sizeof(x.m_s.m_Value) + nPadS);
	if (nPadS)
		DataOut_be<uint8_t>(0);
	DataOutBlob(x.m_s);
}

void Loader::EstablishSChannel(uint32_t nTargetID)
{
	Exc::CheckpointTxt cp1("Ledger SChannel");

	//{
	//	// test
	//	PubKey pk;
	//	pk.m_ptS.m_X.Scan("747f3ed897fb7585cf042412937d0949631da22b61b9ebe3159f3a54a18b7017");
	//	pk.m_ptS.m_Y.Scan("797722ceb77660ced79eae48009741559bff446ae3bbb04ceff61ec1e92d163f");

	//	Ecdsa sig;
	//	sig.m_r.m_Value.Scan("e9cb9ccc21173597512f41c372ba376f3180af9454532e53f1d2e2329be2a7f3");
	//	sig.m_s.m_Value.Scan("1f60c17578f0627e9d87ff5d03c3f642876ef4001947469a2954c31eeeced663");

	//	ECC::Hash::Processor hp;
	//	hp << 1u;
	//	hp.Write(&pk, sizeof(pk));
	//	ECC::Hash::Value hv;
	//	hp >> hv; // message

	//	ECC::Point::Native ptN;
	//	verify_test(ptN.Import(pk.m_ptS, true));
	//	verify_test(sig.IsValid(ptN, hv));
	//}


	//{
	//	// test
	//	PubKey pk;
	//	pk.m_ptS.m_X.Scan("50bd343141a0f12c79bba2fb7119c08b64cd596f7493984d2939903513d69bbf");
	//	pk.m_ptS.m_Y.Scan("47ab7182e9c24c57c7bef177c4dd449679d6a877506a10a1669383491f7e4319");

	//	Ecdsa sig;
	//	sig.m_r.m_Value.Scan("bb8932fd020d509f0a78fba641b0e59d1a9b5c7c742679d301f26f376cc6b2a0");
	//	sig.m_s.m_Value.Scan("1b282223c89e83ba7b7cd6a45684e7a8f59fd3c21728a47c3d8d6801b01a57de");

	//	uintBig_t<8> hvNonceMy, hvNonceDev;
	//	hvNonceMy.Scan("cb9f239f243081ca");
	//	hvNonceDev.Scan("2ed9ad91aba4b30f");

	//	ECC::Hash::Processor hp;
	//	hp
	//		<< 0x11u
	//		<< hvNonceMy
	//		<< hvNonceDev;
	//	hp.Write(&pk, sizeof(pk));

	//	ECC::Hash::Value hv;
	//	hp >> hv; // message

	//	ECC::Point::Native ptN;
	//	verify_test(ptN.Import(pk.m_ptS, true));
	//	verify_test(sig.IsValid(ptN, hv));
	//}


	//{
	//	ECC::Point::Storage ptS;
	//	ptS.m_X.Scan("f494e2e397cdfe0383fad6de8d6f1b97b4dbf208b2e6bc8000f53ecd76934714");
	//	ptS.m_Y.Scan("429dfcac2f4c0939129e098d2f88374dd1d0a4c6b3bf63c4b8b0189a083355c3");

	//	ECC::Point::Native ptN;
	//	verify_test(ptN.Import(ptS, true));

	//	ECC::Scalar k;
	//	k.m_Value.Scan("fa22cb299ca02730d52538a4bf5fb618d4b20981b101b507057bd59cf6572b46");
	//	ptN = ptN * k;

	//	ECC::Point pt;
	//	ptN.Export(pt);

	//	ECC::Hash::Processor hp;
	//	uint8_t nCode = 0x2 | pt.m_Y;
	//	hp << nCode;
	//	hp << pt.m_X;

	//	ECC::Hash::Value hv1, hv2;
	//	hp >> hv1;
	//	hv2.Scan("3b191f3fd3fa130ff635a367aa56ed3b08a01e472cdd397cd12695b5fbc84840");

	//	verify_test(hv1 == hv2);
	//}

	//{
	//	ECC::Hash::Value hvSecret;
	//	hvSecret.Scan("9e220686b42054a5715e71fcc51a9ce9f21b561cabc2e0fc1d24c8369cc42f5c");
	//	uintBig_t<16> k1, k2;

	//	m_Enc.Init(hvSecret, 0);
	//	m_Mac.Init(hvSecret, 1);

	//	uint8_t pInp[] = { 0xc, 4, 'B','e','a','m' };

	//	uint8_t pEnc[0x100];
	//	memcpy(pEnc, pInp, sizeof(pInp));
	//	uint32_t nLenEnc = sizeof(pInp);
	//	pEnc[nLenEnc++] = 0x80;
	//	while (nLenEnc & 0xf)
	//		pEnc[nLenEnc++] = 0;

	//	m_Enc.Encode(pEnc, pEnc, nLenEnc);
	//	m_Mac.Encode(pEnc, pEnc, nLenEnc);
	//}

	m_Data = 0;
	DataOut_be(nTargetID);
	TestStatus(Exchange(Cmd(4)));

	uintBig_t<8> hvNonceMy, hvNonceDev;
	ECC::GenRandom(hvNonceMy);

	m_Data = 0;
	DataOutBlob(hvNonceMy);
	TestStatus(Exchange(Cmd(0x50)));

	DataIn_be<uint32_t>(); // batch_signer_serial
	DataInBlob(hvNonceDev);

	// master key
	ECC::Scalar::Native skMaster;
	skMaster.GenRandomNnz();

	PubKey pk;
	{
		ECC::Point::Native ptN = ECC::Context::get().G * skMaster;
		ptN.Export(pk.m_ptS);
	}

	m_Data = 0;
	DataOutBlob<uint8_t>(sizeof(pk));
	DataOutBlob(pk);

	{
		// sign
		ECC::Hash::Processor hp;
		hp << 1u;
		hp.Write(&pk, sizeof(pk));

		ECC::Hash::Value hv;
		hp >> hv; // message

		Ecdsa sig;
		sig.Sign(skMaster, hv);
		DataOutSig(sig);
	}

	TestStatus(Exchange(Cmd(0x51)));


	// ephemeral key
	ECC::Scalar::Native skEphemeral;
	skEphemeral.GenRandomNnz();

	{
		ECC::Point::Native ptN = ECC::Context::get().G * skEphemeral;
		ptN.Export(pk.m_ptS);
	}

	m_Data = 0;
	DataOutBlob<uint8_t>(sizeof(pk));
	DataOutBlob(pk);

	{
		// sign
		ECC::Hash::Processor hp;
		hp
			<< 0x11u
			<< hvNonceMy
			<< hvNonceDev;
		hp.Write(&pk, sizeof(pk));

		ECC::Hash::Value hv;
		hp >> hv; // message

		Ecdsa sig;
		sig.Sign(skMaster, hv);
		DataOutSig(sig);
	}

	TestStatus(Exchange(Cmd(0x51, 0x80)));

	// get dev cert.
	m_Data = 0;
	TestStatus(Exchange(Cmd(0x52)));

	// get dev cert #2. Skip cert chain, go straight to 'loading from user key'
	m_Data = 0;
	TestStatus(Exchange(Cmd(0x52, 0x80)));

	{
		auto n = *DataIn(1);
		DataIn(n); // skip ret hdr

		auto nSize = *DataIn(1);
		TestInVal<uint16_t>(sizeof(PubKey), nSize);

		auto pPk = (const PubKey*) DataIn(sizeof(PubKey));
		TestInVal<uint16_t>(pk.m_Tag, pPk->m_Tag);

		ECC::Point::Native ptN;
		if (!ptN.Import(pPk->m_ptS, true))
			Exc::Fail("bad dev pk");

		// Derive DH secret (secp256k1 style)
		ptN = ptN * skEphemeral;

		ECC::NoLeak<ECC::Point> pt;
		ptN.Export(pt.V);

		ECC::Hash::Processor hp;
		uint8_t nCode = 0x2 | pt.V.m_Y;
		hp << nCode;
		hp << pt.V.m_X;

		ECC::NoLeak<ECC::Hash::Value> hv;
		hp >> hv.V;
			
		m_Enc.Init(hv.V, 0);
		m_Mac.Init(hv.V, 1);
	}

	// go to encrypted mode
	m_Data = 0;
	TestStatus(Exchange(Cmd(0x53)));

}

uint32_t Loader::GetVersion(std::string& sMcuVer)
{
	m_Data = 0;
	DataOut_be<uint8_t>(0x10);
	TestStatus(ExchangeSec(Cmd()));

	uint32_t nTargetID = DataIn_be<uint32_t>();

	uint8_t nLenMcu = DataIn_be<uint8_t>();
	sMcuVer.resize(nLenMcu);
	if (nLenMcu)
		memcpy(&sMcuVer.front(), DataIn(nLenMcu), nLenMcu);

	return nTargetID;
}

void Loader::DeleteApp(const std::string& sApp)
{
	Exc::CheckpointTxt cp1("Delete app");

	m_Data = 0;
	DataOut_be<uint8_t>(0xc);

	uint8_t n = (uint8_t) sApp.size();
	DataOut_be<uint8_t>(n);
	DataOut(sApp.c_str(), n);

	TestStatus(ExchangeSec(Cmd()));
}

template <typename TContainer>
void BufAddVarArg(ByteBuffer& buf, uint8_t tag, const TContainer& x)
{
	buf.push_back(tag);
	uint8_t nLen = (uint8_t)x.size();
	buf.push_back(nLen);
	buf.insert(buf.end(), x.begin(), x.end());
}

void Loader::Install(const AppData& ad)
{
	ByteBuffer bufInstArgs;
	BufAddVarArg(bufInstArgs, 0x01, ad.m_sName);
	BufAddVarArg(bufInstArgs, 0x02, ad.m_sAppVer);
	BufAddVarArg(bufInstArgs, 0x03, ad.m_Icon);
	BufAddVarArg(bufInstArgs, 0x04, ad.m_KeyPath);

	auto rit = ad.m_Zones.rbegin();
	uint32_t nAddrBegin = ad.m_Zones.begin()->first;
	uint32_t nAddrEnd = rit->first + (uint32_t)rit->second.size();

	uint32_t pCp[5];
	pCp[0] = nAddrEnd - nAddrBegin - ad.m_SizeNVRam; // Code length
	pCp[1] = ad.m_SizeNVRam; // data length
	pCp[2] = (uint32_t) bufInstArgs.size();
	pCp[3] = 0; // flags
	pCp[4] = ad.m_BootAddr - nAddrBegin; // boot offset

	for (uint32_t i = 0; i < _countof(pCp); i++)
		pCp[i] = ByteOrder::to_be(pCp[i]);

	ECC::Hash::Value hv;
	{
		// calculate expected hash
		ECC::Hash::Processor hp;
		hp << uintBigFrom(ad.m_TargetID);
		hp.Write(ad.m_sTargetVer.c_str(), (uint32_t) ad.m_sTargetVer.size());
		hp.Write(pCp, sizeof(pCp));

		for (auto it = ad.m_Zones.begin(); ad.m_Zones.end() != it; it++)
		{
			const auto& seg = it->second;
			hp.Write(&seg.front(), (uint32_t)seg.size());
		}

		hp.Write(&bufInstArgs.front(), (uint32_t) bufInstArgs.size());
		hp >> hv;
	}

	std::cout << "Expected app Hash: " << hv.str() << std::endl;

	std::cout << "Connecting to the device. Please approve the manager..." << std::endl;
	EstablishSChannel(ad.m_TargetID);

	{
		std::string sMcuVer;
		GetVersion(sMcuVer);
		if (sMcuVer != ad.m_sTargetVer)
		{
			std::cout << "Unsupported firmware version. Expected=" << ad.m_sTargetVer << ", Actual=" << sMcuVer << std::endl;
			std::cout << "Please update device firmware first" << std::endl;
			Exc::Fail();
		}
	}

	std::cout << "Deleting previous app installation (if exists). Please approve..." << std::endl;
	DeleteApp(ad.m_sName);


	std::cout << "Loading app..." << std::endl;

	m_Data = 0;
	DataOut_be<uint8_t>(0xb); // set create app params
	DataOut(pCp, sizeof(pCp));

	TestStatus(ExchangeSec(Cmd()));

	for (auto it = ad.m_Zones.begin(); ad.m_Zones.end() != it; it++)
	{
		m_Data = 0;
		DataOut_be<uint8_t>(0x5); // select segment
		DataOut_be<uint32_t>(it->first - nAddrBegin);
		TestStatus(ExchangeSec(Cmd()));

		const auto& buf = it->second;
		uint32_t nEnd = (uint32_t) buf.size();

		for (uint32_t nPos = 0; nPos < nEnd; )
		{
			auto nChunk = std::min<uint32_t>(nEnd - nPos, 220);

			m_Data = 0;
			DataOut_be<uint8_t>(0x6); // chunk
			DataOut_be((uint16_t) nPos);
			DataOut(&buf.front() + nPos, (uint8_t) nChunk);
			TestStatus(ExchangeSec(Cmd()));

			nPos += nChunk;
		}

		m_Data = 0;
		DataOut_be<uint8_t>(0x7); // flush segment
		TestStatus(ExchangeSec(Cmd()));
	}

	// install args
	{
		m_Data = 0;
		DataOut_be<uint8_t>(0x5); // select segment
		DataOut_be(nAddrEnd - nAddrBegin);
		TestStatus(ExchangeSec(Cmd()));

		m_Data = 0;
		DataOut_be<uint8_t>(0x6); // chunk
		DataOut_be<uint16_t>(0);
		DataOut(&bufInstArgs.front(), (uint8_t) bufInstArgs.size());
		TestStatus(ExchangeSec(Cmd()));

		m_Data = 0;
		DataOut_be<uint8_t>(0x7); // flush segment
		TestStatus(ExchangeSec(Cmd()));
	}

	std::cout << "Please approve app install..." << std::endl;

	m_Data = 0;
	DataOut_be<uint8_t>(0x9); // commit
	TestStatus(ExchangeSec(Cmd()));

	std::cout << "done!" << std::endl;
}


void FindAndLoad(const char* szPath)
{
	AppData ad;
	ad.Load(szPath);

	Loader ld;

	auto ret = HidInfo::EnumSupported();
	for (uint32_t i = 0; ; i++)
	{
		if (ret.size() == i)
			Exc::Fail("No supported devices found");

		const auto& v = ret[i];

		std::cout << "Found supported device: " << v.m_sManufacturer << " " << v.m_sProduct << std::endl;
		if (v.m_Product == ad.m_HidProductID)
		{
			ld.m_Io.Open(v.m_sPath.c_str());
			break;
		}

		std::cout << "Incompatible with the app data. Skipping.";
	}

	ld.Install(ad);
}




} // namespace LedgerFw
} // namespace beam::wallet

