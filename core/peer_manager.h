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
		//	Initially set to default (non-zero)
		//	Increased after a valid data is received from this peer (minor for header and transaction, major for a block)
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
		//		Adjusted rating, which is increased with the starvation time, i.e. how long ago it was connected
		//	The selection of the peer to performed by selecting two (non-overlapping) groups.
		//		Those with highest ratings
		//		Those with highest *adjusted* ratings.
		//	So that we effectively always try to maintain connection with the best peers, but also shuffle and connect to others.
		//
		//	There is a min threshold for connection time, i.e. we won't disconnect shortly after connecting because the rating of this peer went slightly below another candidate

		struct Rating
		{
			static const uint32_t Initial = 1024;
			static const uint32_t RewardHeader = 64;
			static const uint32_t RewardTx = 16;
			static const uint32_t RewardBlock = 512;
			static const uint32_t PenaltyTimeout = 256;
			static const uint32_t PenaltyNetworkErr = 128;
			static const uint32_t Max = 10240; // saturation

			static uint32_t Saturate(uint32_t);
			static void Inc(uint32_t& r, uint32_t delta);
			static void Dec(uint32_t& r, uint32_t delta);
		};

		struct Cfg {
			uint32_t m_DesiredHighest = 5;
			uint32_t m_DesiredTotal = 10;
			uint32_t m_TimeoutDisconnect_ms = 1000 * 60 * 2; // connected for less than 2 minutes -> penalty
			uint32_t m_TimeoutReconnect_ms	= 1000;
			uint32_t m_TimeoutBan_ms		= 1000 * 60 * 10;
			uint32_t m_TimeoutAddrChange_s	= 60 * 60 * 2;
			uint32_t m_StarvationRatioInc	= 1; // increase per second while not connected
			uint32_t m_StarvationRatioDec	= 2; // decrease per second while connected (until starvation reward is zero)
		} m_Cfg;


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
				uint32_t m_Increment;
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
		void ModifyRating(PeerInfo&, uint32_t, bool bAdd);
		void Ban(PeerInfo&);
		void OnSeen(PeerInfo&);
		void OnRemoteError(PeerInfo&, bool bShouldBan);

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

	private:
		PeerIDSet m_IDs;
		RawRatingSet m_Ratings;
		AdjustedRatingSet m_AdjustedRatings;
		AddrSet m_Addr;
		ActiveList m_Active;
		uint32_t m_TicksLast_ms = 0;

		void UpdateRatingsInternal(uint32_t t_ms);

		void ActivatePeerInternal(PeerInfo&, uint32_t nTicks_ms, uint32_t& nSelected);
		void ModifyRatingInternal(PeerInfo&, uint32_t, bool bAdd, bool ban);
	};

	std::ostream& operator << (std::ostream& s, const PeerManager::PeerInfo&);

} // namespace beam
