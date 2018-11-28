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
#include "block_crypt.h"

namespace beam
{
	struct Treasury
	{
		struct Request
		{
			PeerID m_WalletID;

			struct Group
			{
				struct Coin
				{
					Amount m_Value;
					Height m_Incubation;
				};
				std::vector<Coin> m_vCoins;

				void AddSubsidy(AmountBig& res);
			};

			std::vector<Group> m_vGroups;
		};

		struct Response
		{
			struct Group
			{
				struct Coin
				{
					Output::Ptr m_pOutput;
					ECC::Signature m_Sig; // proves the amount

					void get_SigMsg(ECC::Hash::Value& hv) const;
				};
				std::vector<Coin> m_vCoins;

				TxBase m_Base; // contains offset
				TxKernel::Ptr m_pKernel;

				struct Reader;

				bool IsValid(const Request::Group&, ECC::Oracle& oracle) const;
				void Dump(TxBase::IWriter&, TxBase&) const;

				void Create(const Request::Group&, ECC::Oracle& oracle, Key::IKdf&, uint64_t& nIndex);
			};

			std::vector<Group> m_vGroups;

			ECC::Signature m_Sig; // signs all the output commitments, with the key of WalletID

			bool Create(const Request&, Key::IKdf&, uint64_t& nIndex);
			bool IsValid(const Request&) const;
		};

		static void get_ID(Key::IKdf&, PeerID&, ECC::Scalar::Native&);
	};

}
