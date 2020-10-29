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

struct HashObj
{
	enum Type : uint32_t
	{
		Sha256 = 0,
	};
};

