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

static const uint32_t BlockCharge = 100*1000*1000; // 100 mln units

template <uint32_t nMaxOps>
struct ChargeFor {
	static const uint32_t V = (BlockCharge + nMaxOps - 1) / nMaxOps;
	static_assert(V, "");
};

struct Cost
{
	static const uint32_t Cycle				= ChargeFor<20*1000*1000>::V;
	static const uint32_t MemOp				= ChargeFor<2*1000*1000>::V;
	static const uint32_t MemOpPerByte		= ChargeFor<50*1000*1000>::V;
	static const uint32_t HeapOp			= ChargeFor<1000*1000>::V;
	static const uint32_t LoadVar			= ChargeFor<20*1000>::V;
	static const uint32_t LoadVarPerByte	= ChargeFor<2*1000*1000>::V;
	static const uint32_t SaveVar			= ChargeFor<5*1000>::V;
	static const uint32_t SaveVarPerByte	= ChargeFor<1000*1000>::V;
	static const uint32_t Log				= ChargeFor<20*1000>::V;
	static const uint32_t LogPerByte		= ChargeFor<1000*1000>::V;
	static const uint32_t UpdateShader		= ChargeFor<1*1000>::V;
	static const uint32_t CallFar			= ChargeFor<10*1000>::V;
	static const uint32_t AddSig			= ChargeFor<10*1000>::V;
	static const uint32_t AssetManage		= ChargeFor<1000>::V;
	static const uint32_t AssetEmit			= ChargeFor<20*1000>::V;
	static const uint32_t FundsLock			= ChargeFor<50*1000>::V;
	static const uint32_t HashOp			= ChargeFor<1000*1000>::V;
	static const uint32_t HashWrite			= ChargeFor<5 * 1000*1000>::V;
	static const uint32_t HashWritePerByte	= ChargeFor<50*1000*1000>::V;

	static const uint32_t Secp_ScalarInv		= ChargeFor<5*1000>::V;
	static const uint32_t Secp_Point_Import		= ChargeFor<5*1000>::V;
	static const uint32_t Secp_Point_Export		= ChargeFor<5*1000>::V;
	static const uint32_t Secp_Point_Multiply	= ChargeFor<2*1000>::V;

	static const uint32_t BeamHashIII		= ChargeFor<20*1000>::V;

	static const uint32_t Refs = LoadVar + SaveVar;

	static uint32_t LoadVar_For(uint32_t nValSize) {
		return Cost::LoadVar + Cost::LoadVarPerByte * nValSize;
	}

	static uint32_t SaveVar_For(uint32_t nValSize) {
		return Cost::SaveVar + Cost::SaveVarPerByte * nValSize;
	}

	static uint32_t Log_For(uint32_t nValSize) {
		return Cost::Log + Cost::LogPerByte * nValSize;
	}

	static uint32_t UpdateShader_For(uint32_t nValSize) {
		return Cost::UpdateShader + Cost::SaveVarPerByte * nValSize;
	}

};
