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


namespace beam {

	struct MiniBlockChain
	{
		struct State
		{
			Block::SystemState::Full m_Hdr;
			std::unique_ptr<uint8_t[]> m_pMmrData;
		};

		std::vector<State> m_vStates;
		Merkle::Hash m_hvLive;

		struct DMmr
			:public Merkle::DistributedMmr
		{
			virtual const void* get_NodeData(Key key) const
			{
				return get_ParentObj().m_vStates[key].m_pMmrData.get();
			}

			virtual void get_NodeHash(Merkle::Hash& hv, Key key) const
			{
				get_ParentObj().m_vStates[key].m_Hdr.get_Hash(hv);
			}

			IMPLEMENT_GET_PARENT_OBJ(MiniBlockChain, m_Mmr)
		} m_Mmr;

		struct Source
			:public Block::ChainWorkProof::ISource
		{
			virtual void get_StateAt(Block::SystemState::Full& s, const Difficulty::Raw& d) override
			{
				// median search. The Hdr.m_ChainWork must be strictly bigger than d. (It's exclusive)
				typedef std::vector<State>::const_iterator Iterator;
				Iterator it0 = get_ParentObj().m_vStates.begin();
				Iterator it1 = get_ParentObj().m_vStates.end();

				while (it0 < it1)
				{
					Iterator itMid = it0 + (it1 - it0) / 2;
					if (itMid->m_Hdr.m_ChainWork <= d)
						it0 = itMid + 1;
					else
						it1 = itMid;
				}

				s = it0->m_Hdr;
			}

			virtual void get_Proof(Merkle::IProofBuilder& bld, Height h) override
			{
				assert(h >= Rules::HeightGenesis);
				h -= Rules::HeightGenesis;

				assert(h < get_ParentObj().m_vStates.size());

				get_ParentObj().m_Mmr.get_Proof(bld, h);
			}

			IMPLEMENT_GET_PARENT_OBJ(MiniBlockChain, m_Source)
		} m_Source;

		MiniBlockChain()
		{
			m_hvLive = 55U; // whatever
		}

		void Add()
		{
			size_t i = m_vStates.size();
			m_vStates.emplace_back();
			State& s = m_vStates.back();

			if (i)
			{
				State& s0 = m_vStates[i - 1];
				s.m_Hdr = s0.m_Hdr;
				s.m_Hdr.NextPrefix();
                s.m_Hdr.m_TimeStamp = getTimestamp();

				uint32_t nSize = m_Mmr.get_NodeSize(i - 1);
				s0.m_pMmrData.reset(new uint8_t[nSize]);
				m_Mmr.Append(i - 1, s0.m_pMmrData.get(), s.m_Hdr.m_Prev);
			}
			else
			{
				ZeroObject(s.m_Hdr);
				s.m_Hdr.m_Height = Rules::HeightGenesis;
				s.m_Hdr.m_Prev = Rules::get().Prehistoric;
				s.m_Hdr.m_PoW.m_Difficulty = Rules::get().DA.Difficulty0;
                s.m_Hdr.m_TimeStamp = getTimestamp();
			}

			if (!((i + 1) % 8000))
			{
				// slightly raise
				Difficulty::Raw raw;
				s.m_Hdr.m_PoW.m_Difficulty.Unpack(raw);
				s.m_Hdr.m_PoW.m_Difficulty.Calculate(raw, 1, 150, 140);
			}

			s.m_Hdr.m_ChainWork += s.m_Hdr.m_PoW.m_Difficulty;

			m_Mmr.get_Hash(s.m_Hdr.m_Definition);
			Merkle::Interpret(s.m_Hdr.m_Definition, m_hvLive, true);
			s.m_Hdr.m_Kernels = Zero;
		}

		void Generate(Height dh)
		{
			while (dh--)
				Add();
		}
	};

} // namespace beam