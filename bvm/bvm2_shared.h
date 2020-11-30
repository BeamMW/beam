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

#pragma pack (pop)

struct HashObj;

struct BlockHeader
{
	Height m_Height;
	HashValue m_Hash;
	Timestamp m_Timestamp;
	HashValue m_Kernels;
	HashValue m_Definition;

	template <bool bToShader>
	void Convert()
	{
		ConvertOrd<bToShader>(m_Height);
		ConvertOrd<bToShader>(m_Timestamp);
	}
};

struct KeyTag
{
	static const uint8_t Internal = 0;
	static const uint8_t LockedAmount = 1;
	static const uint8_t Refs = 2;
	static const uint8_t OwnedAsset = 3;

	// Synthetic tags, not really contract vars
	static const uint8_t SidCid = 16; // Key={00...00}tag{sid}{cid}, Value=BigEndian(createHeight)
};
