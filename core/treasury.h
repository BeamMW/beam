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

					template <typename Archive>
					void serialize(Archive& ar)
					{
						ar
							& m_Value
							& m_Incubation;
					}

				};

				std::vector<Coin> m_vCoins;

				void AddSubsidy(AmountBig::Type& res) const;

				template <typename Archive>
				void serialize(Archive& ar)
				{
					ar & m_vCoins;
				}
			};

			std::vector<Group> m_vGroups;

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_WalletID
					& m_vGroups;
			}
		};

		struct Response
		{
			PeerID m_WalletID;

			struct Group
			{
				struct Coin
				{
					Output::Ptr m_pOutput;
					ECC::Signature m_Sig; // proves the amount

					void get_SigMsg(ECC::Hash::Value& hv) const;

					template <typename Archive>
					void serialize(Archive& ar)
					{
						ar
							& m_pOutput
							& m_Sig;
					}
				};

				std::vector<Coin> m_vCoins;

				TxBase m_Base; // contains offset
				TxKernel::Ptr m_pKernel;

				template <typename Archive>
				void serialize(Archive& ar)
				{
					ar
						& m_vCoins
						& m_pKernel
						& m_Base;
				}

				struct Reader;

				bool IsValid(const Request::Group&) const;
				void Create(const Request::Group&, Key::IKdf&, uint64_t& nIndex);
			};

			std::vector<Group> m_vGroups;

			ECC::Signature m_Sig; // signs all the output commitments, with the key of WalletID

			void HashOutputs(ECC::Hash::Value&) const;
			bool Create(const Request&, Key::IKdf&, uint64_t& nIndex);
			bool IsValid(const Request&) const;

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_WalletID
					& m_vGroups
					& m_Sig;
			}
		};

		static void get_ID(Key::IKdf&, PeerID&, ECC::Scalar::Native&);

		struct Parameters
		{
			Height m_MaturityStep = 1440 * 30; // 1 month roughly
			uint32_t m_Bursts = 12 * 5; // 5 years plan

		};

		struct Entry
		{
			Request m_Request;
			std::unique_ptr<Response> m_pResponse;

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_Request
					& m_pResponse;
			}
		};

		typedef std::map<PeerID, Entry> EntryMap;
		EntryMap m_Entries;

		struct Data
		{
			struct Group
			{
				Transaction m_Data;
				AmountBig::Type m_Value;

				bool IsValid() const;

				template <typename Archive>
				void serialize(Archive& ar)
				{
					ar & m_Data;
					ar & m_Value;
				}
			};

			std::string m_sCustomMsg;
			std::vector<Group> m_vGroups;

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_sCustomMsg
					& m_vGroups;
			}

			bool IsValid() const;

			struct Coin
			{
				Height m_Incubation;
				Key::IDV m_Kidv;

				int cmp(const Coin&) const;
				COMPARISON_VIA_CMP
			};

			void Recover(Key::IPKdf&, std::vector<Coin>&) const;
		};

		Entry* CreatePlan(const PeerID&, Amount nPerBlockAvg, const Parameters&);
		void Build(Data&) const;

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar & m_Entries;
		}

		class ThreadPool;
	};

}
