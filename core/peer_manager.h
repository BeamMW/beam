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

#include "common.h"
#include "block_crypt.h"
#include "../utility/io/address.h"
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>

namespace beam {

	class PeerManager
	{
	public:

		// Rating system:
		//	Represents the "effective" bandwidth" on a logarithmic scale, i.e. Rating = A*Log(Bw/norm)
		//	Initially set to default (non-zero)
		//	Adjusted each time the data download (by request) has been completed
		//	Decreased if the peer fails to accomplish the data request ()
		//	Decreased on network error shortly after connect/accept (or inability to connect)
		//	Reset to 0 for banned peers. Triggered upon:
		//		Any protocol violation (including running with incompatible configuration)
		//		invalid block received from this peer
		//
		// Policy wrt peers:
		//	Connection to banned peers is disallowed for at least specified time period (even if no other options left)
		//	We calculate two ratings for all the peers:
		//		Raw rating, based on its behavior
		//		Adjusted rating, which is increased with the starvation time, i.e. how long ago anything was requested from this peer
		//	The selection of the peer to performed by selecting two (non-overlapping) groups.
		//		Those with highest ratings
		//		Those with highest *adjusted* ratings.
		//	So that we effectively always try to maintain connection with the best peers, but also shuffle and connect to others.
		//
		//	There is a min threshold for connection time, i.e. we won't disconnect shortly after connecting because the rating of this peer went slightly below another candidate
		//
		//	At any moment when we need data - it's requested from the connected peer with max adjusted rating. The "starvation bonus" for this peer is immediately reset.

		struct Rating
		{
			static const uint32_t Initial = 1024;

			static const uint32_t PenaltyNetworkErr = 128;

			static const uint32_t Starvation_s_ToRatio = 1; // increase per second

			// Our Bps -> rating convertion formula:
			//	Rating = A * log (Bps / norm)
			// 
			// We want x2 bw difference be equivalent to ~120 rating units. Means, for a x2 difference the slower peer gets prioity after 2 minutes of starvation.
			// Hence: A = 172 (roughly)
			//
			// The initial rating (for unknown peer) considered to be ~100 KBps.
			// Hence: 1024 = 172 * log(100 KBps / norm)
			// norm = 255 Bps (roughly)

			static const uint32_t kA = 172;
			static const uint32_t kNorm = 255;

			static uint32_t FromBps(uint32_t);
			static uint32_t ToBps(uint32_t);

		};

		struct Cfg {
			uint32_t m_DesiredHighest = 5;
			uint32_t m_DesiredTotal = 10;
			uint32_t m_TimeoutDisconnect_ms = 1000 * 60 * 2; // connected for less than 2 minutes -> penalty
			uint32_t m_TimeoutReconnect_ms	= 1000;
			uint32_t m_TimeoutBan_ms		= 1000 * 60 * 10;
			uint32_t m_TimeoutAddrChange_s	= 60 * 60 * 2;
			uint32_t m_TimeoutRecommend_s	= 60 * 60 * 10;
		} m_Cfg;

		class TimePoint
		{
			static thread_local uint32_t s_Value_ms;
			bool m_Set;

		public:
			TimePoint();
			~TimePoint();
			static uint32_t get();
		};

		struct PeerInfo
		{
			struct ID
				:public boost::intrusive::set_base_hook<>
			{
				PeerID m_Key;
				bool operator < (const ID& x) const { return (m_Key < x.m_Key); }

				IMPLEMENT_GET_PARENT_OBJ(PeerInfo, m_ID)
			} m_ID;

			struct RawRating
				:public boost::intrusive::set_base_hook<>
			{
				uint32_t m_Value;
				bool operator < (const RawRating& x) const { return (m_Value > x.m_Value); } // reverse order, begin - max

				IMPLEMENT_GET_PARENT_OBJ(PeerInfo, m_RawRating)
			} m_RawRating;

			struct AdjustedRating
				:public boost::intrusive::set_base_hook<>
			{
				uint32_t m_BoostFrom_ms;
				uint32_t get() const;
				bool operator < (const AdjustedRating& x) const { return (get() > x.get()); } // reverse order, begin - max

				IMPLEMENT_GET_PARENT_OBJ(PeerInfo, m_AdjustedRating)
			} m_AdjustedRating;

			struct Active
				:public boost::intrusive::list_base_hook<>
			{
				bool m_Now;
				bool m_Next; // used internally during switching
				IMPLEMENT_GET_PARENT_OBJ(PeerInfo, m_Active)
			} m_Active;

			struct Addr
				:public boost::intrusive::set_base_hook<>
			{
				io::Address m_Value;
				bool operator < (const Addr& x) const { return (m_Value < x.m_Value); }

				IMPLEMENT_GET_PARENT_OBJ(PeerInfo, m_Addr)
			} m_Addr;

			Timestamp m_LastSeen; // needed to filter-out dead peers, and to know when to update the address
			uint32_t m_LastActivity_ms; // updated on connection attempt, and disconnection.
		};

		typedef boost::intrusive::multiset<PeerInfo::ID> PeerIDSet;
		typedef boost::intrusive::multiset<PeerInfo::RawRating> RawRatingSet;
		typedef boost::intrusive::multiset<PeerInfo::AdjustedRating> AdjustedRatingSet;
		typedef boost::intrusive::multiset<PeerInfo::Addr> AddrSet;
		typedef boost::intrusive::list<PeerInfo::Active> ActiveList;

		void Update(); // will trigger activation/deactivation of peers
		PeerInfo* Find(const PeerID& id, bool& bCreate);

		void OnActive(PeerInfo&, bool bActive);
		void SetRating(PeerInfo&, uint32_t);
		void Ban(PeerInfo&);
		void ResetRatingBoost(PeerInfo&);
		void OnSeen(PeerInfo&);
		bool IsOutdated(const PeerInfo&) const;
		void ModifyAddr(PeerInfo&, const io::Address&);
		void RemoveAddr(PeerInfo&);

		PeerInfo* OnPeer(const PeerID&, const io::Address&, bool bAddrVerified);

		void Delete(PeerInfo&);
		void Clear();

		virtual void ActivatePeer(PeerInfo&) {}
		virtual void DeactivatePeer(PeerInfo&) {}
		virtual PeerInfo* AllocPeer() = 0;
		virtual void DeletePeer(PeerInfo&) = 0;

		const RawRatingSet& get_Ratings() const { return m_Ratings; }
		const AddrSet& get_Addrs() const { return m_Addr; }

	private:
		PeerIDSet m_IDs;
		RawRatingSet m_Ratings;
		AdjustedRatingSet m_AdjustedRatings;
		AddrSet m_Addr;
		ActiveList m_Active;
		uint32_t m_TicksLast_ms = 0;

		void ActivatePeerInternal(PeerInfo&, uint32_t nTicks_ms, uint32_t& nSelected);
		void SetRatingInternal(PeerInfo&, uint32_t, bool ban);
	};

	std::ostream& operator << (std::ostream& s, const PeerManager::PeerInfo&);

} // namespace beam
