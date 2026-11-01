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
#include "db.h"
#include "../bvm/bvm2.h"

namespace Shaders {
#	define HOST_BUILD
#	include "../bvm/Shaders/common.h"
} // namespace Shaders

namespace beam {

	struct LockedFunds
	{
		// funds locked in a contract, per asset
		typedef Shaders::Env::Key_T<uintBigFor<Asset::ID>::Type> Key;

		NodeDB::WalkerContractData m_Wlk;
		Key m_pRange[2]; // must be alive during the enum
		Asset::ID m_Aid;
		AmountBig::Number m_Value;

		void Enum(NodeDB&, const ContractID&);
		bool MoveNext();

		static void SetKey(Key&, const ContractID&, Asset::ID);
		static bool Find(NodeDB&, const ContractID&, Asset::ID, AmountBig::Number&);
		static bool FindNextContract(NodeDB&, ContractID&); // in-out, finds the min contract ID >= the given one
	};

} // namespace beam
