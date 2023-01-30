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
#include "../core/aes2.h"


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



		struct Loader
		{

			beam::wallet::UsbIO m_Io;

			struct Cmd;

			uint8_t m_pData[0x100];
			uint8_t m_Data = 0;
			uint8_t m_Read = 0;

			struct CbcCoder
			{
				AES2::Coder m_Aes;
				uint8_t m_pIv[16];

				void Init(const ECC::Hash::Value& hvSecret, uint32_t iKey);
				void Encode(uint8_t* pDst, const uint8_t* pSrc, uint32_t len);
				void Decode(uint8_t* pDst, const uint8_t* pSrc, uint32_t len);
			};

			CbcCoder m_Enc;
			CbcCoder m_Mac;

			void DataOut(const void* p, uint8_t n) {
				memcpy(m_pData + m_Data, p, n);
				m_Data += n;
			}

			template <typename T>
			void DataOutBlob(const T& x) {
				DataOut(&x, sizeof(x));
			}

			template <typename T>
			void DataOut_be(T x) {
				x = ByteOrder::to_be(x);
				DataOutBlob(x);
			}

			uint8_t* DataIn(uint8_t n)
			{
				if (m_Data - m_Read < n)
					Exc::Fail("not enough data");

				auto pRet = m_pData + m_Read;
				m_Read += n;
				return pRet;
			}

			template <typename T>
			void DataInBlob(T& x) {
				memcpy(&x, DataIn(sizeof(x)), sizeof(x));
			}

			template <typename T>
			T DataIn_be() {
				T x;
				DataInBlob(x);
				return ByteOrder::from_be(x);
			}

			struct PubKey {
				uint8_t m_Tag = 4;
				ECC::Point::Storage m_ptS;
			};

			uint16_t Exchange(const Cmd& cmd);
			uint16_t ExchangeSec(const Cmd& cmd);

			static void TestStatus(uint16_t res);

			//static void TestSize(uint16_t n);

			template <typename T>
			void TestInVal(const T& nExp, const T& nActial);

			struct Ecdsa
			{
				ECC::Scalar m_r;
				ECC::Scalar m_s;

				// [r, s], whereas
				//		r = (G*nonce).x
				//		s = (msg + r*sk)/nonce
				// verification:
				//		G*msg/s + Pubkey*r/s = (G*msg + G*sk*r) * noce / (msg+r*sk) = G*nonce
				//		(G*msg/s).x =?= r


				void Sign(const ECC::Scalar::Native& sk, const ECC::Hash::Value& msg);
				bool IsValid(const ECC::Point::Native& pubKey, const ECC::Hash::Value& msg) const;
			};

			void DataOutSig(const Ecdsa& x);

			void EstablishSChannel(uint32_t nTargetID);

			uint32_t GetVersion(std::string& sMcuVer);

			void DeleteApp(const char* szApp);

		};



	} // namespace LedgerFw
} // namespace beam::wallet
