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

struct ILoadVarCallback {
	virtual uint8_t OnVar(uint8_t nTag, const uint8_t* pKey, uint32_t nKey, const uint8_t* pVal, uint32_t nVal) = 0;
};

#pragma pack (push, 1)

struct FundsChange
{
	Amount m_Amount;
	AssetID m_Aid;
	uint8_t m_Consume;

	template <bool bToShader>
	void Convert()
	{
		ConvertOrd<bToShader>(m_Amount);
		ConvertOrd<bToShader>(m_Aid);
		ConvertOrd<bToShader>(m_Consume);
	}
};

struct SigRequest
{
	const void* m_pID;
	uint32_t m_nID;
};

struct HashObj;
struct Secp_scalar;
struct Secp_point;

struct BlockHeader
{
	struct InfoBase
	{
		Timestamp m_Timestamp;
		HashValue m_Kernels;
		HashValue m_Definition;

		template <bool bToShader>
		void Convert()
		{
			ConvertOrd<bToShader>(m_Timestamp);
		}
	};

	struct Info
		:public InfoBase
	{
		Height m_Height;
		HashValue m_Hash;

		template <bool bToShader>
		void Convert()
		{
			ConvertOrd<bToShader>(m_Height);
			InfoBase::Convert<bToShader>();
		}
	};

	struct Prefix
	{
		Height m_Height;
		HashValue m_Prev;
		HashValue m_ChainWork; // not hash, just same size

		template <bool bToShader>
		void Convert()
		{
			ConvertOrd<bToShader>(m_Height);
		}
	};

	struct Element
		:public InfoBase
	{
		struct PoW
		{
			uint8_t m_pIndices[104];
			uint8_t m_pNonce[8];
			uint32_t m_Difficulty;

		} m_PoW;

		template <bool bToShader>
		void Convert()
		{
			InfoBase::Convert<bToShader>();
			ConvertOrd<bToShader>(m_PoW.m_Difficulty);
		}
	};

	struct Full
		:public Prefix
		,public Element
	{
		template <bool bToShader>
		void Convert()
		{
			Prefix::Convert<bToShader>();
			Element::Convert<bToShader>();
		}

		void get_Hash(HashValue& out, const HashValue* pRules) const;

		template <bool bUseEnv = true>
		bool IsValid(const HashValue* pRules) const;

	protected:
		void get_HashInternal(HashValue& out, bool bFull, const HashValue* pRules) const;
		bool TestDifficulty() const;
	};
};


struct KeyTag
{
	static const uint8_t Internal = 0;
	static const uint8_t InternalStealth = 8;
	static const uint8_t LockedAmount = 1;
	static const uint8_t Refs = 2;
	static const uint8_t OwnedAsset = 3;
	static const uint8_t ShaderChange = 4; // from HF4: event when contract shader changes (including creaton and destruction)

	// Synthetic tags, not really contract vars
	static const uint8_t SidCid = 16; // Key={00...00}tag{sid}{cid}, Value=BigEndian(createHeight)

	static const uint32_t s_MaxSize = 256;
};

struct KeySearchFlags
{
	static const uint8_t Exact = 1;
	static const uint8_t Bigger = 2;
};

namespace Merkle
{
	struct Node
	{
		uint8_t m_OnRight;
		HashValue m_Value;
	};
}

struct Stream
{
	static const uint32_t Out = 0;
	static const uint32_t Error = 1;
};

static const uint32_t s_NonceSlots = 256;

struct ApiVersion
{
	static const uint32_t Current = 1;
};

struct CallFarFlags
{
	static const uint32_t InheritContext  = 0x0001; // similar to delegate call
	static const uint32_t SelfBlock       = 0x0200; // forbid recursive call to self
	static const uint32_t SelfLockRO      = 0x0400; // recursive call is allowed, but can't modify state (including explicit and implicit vars, lock/unlock funds, asset ops)
	static const uint32_t GlobalLockRO    = 0x0800; // All modifications are disallowed by all child callees (similar to staticcall)
};

#pragma pack (pop)
