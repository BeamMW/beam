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

#include "peer_manager.h"
#include "../utility/logger.h"

namespace beam {

uint32_t PeerManager::Rating::Saturate(uint32_t v)
{
	// try not to take const refernce on Max, so its value can directly be substituted (otherwise gcc link error)
	return (v < Max) ? v : Max;
}

void PeerManager::Rating::Inc(uint32_t& r, uint32_t delta)
{
	r = Saturate(r + delta);
}

void PeerManager::Rating::Dec(uint32_t& r, uint32_t delta)
{
	r = (r > delta) ? (r - delta) : 1;
}

uint32_t PeerManager::PeerInfo::AdjustedRating::get() const
{
	return Rating::Saturate(get_ParentObj().m_RawRating.m_Value + m_Increment);
}

void PeerManager::Update()
{
	uint32_t nTicks_ms = GetTimeNnz_ms();

	if (m_TicksLast_ms)
		UpdateRatingsInternal(nTicks_ms);

	m_TicksLast_ms = nTicks_ms;

	// select recommended peers
	uint32_t nSelected = 0;

	for (ActiveList::iterator it = m_Active.begin(); m_Active.end() != it; it++)
	{
		PeerInfo& pi = it->get_ParentObj();
		assert(pi.m_Active.m_Now);

		bool bTooEarlyToDisconnect = (nTicks_ms - pi.m_LastActivity_ms < m_Cfg.m_TimeoutDisconnect_ms);

		it->m_Next = bTooEarlyToDisconnect;
		if (bTooEarlyToDisconnect)
			nSelected++;
	}

	// 1st group
	uint32_t nHighest = 0;
	for (RawRatingSet::iterator it = m_Ratings.begin(); (nHighest < m_Cfg.m_DesiredHighest) && (nSelected < m_Cfg.m_DesiredTotal) && (m_Ratings.end() != it); it++, nHighest++)
		ActivatePeerInternal(it->get_ParentObj(), nTicks_ms, nSelected);

	// 2nd group
	for (AdjustedRatingSet::iterator it = m_AdjustedRatings.begin(); (nSelected < m_Cfg.m_DesiredTotal) && (m_AdjustedRatings.end() != it); it++)
		ActivatePeerInternal(it->get_ParentObj(), nTicks_ms, nSelected);

	// remove excess
	for (ActiveList::iterator it = m_Active.begin(); m_Active.end() != it; )
	{
		PeerInfo& pi = (it++)->get_ParentObj();
		assert(pi.m_Active.m_Now);

		if (!pi.m_Active.m_Next)
		{
			OnActive(pi, false);
			DeactivatePeer(pi);
		}
	}
}

void PeerManager::ActivatePeerInternal(PeerInfo& pi, uint32_t nTicks_ms, uint32_t& nSelected)
{
	if (pi.m_Active.m_Now && pi.m_Active.m_Next)
		return; // already selected

	if (pi.m_Addr.m_Value.empty())
		return; // current adddress unknown

	if (!pi.m_Active.m_Now && (nTicks_ms - pi.m_LastActivity_ms < m_Cfg.m_TimeoutReconnect_ms))
		return; // too early for reconnect

	if (!pi.m_RawRating.m_Value)
		return; // banned so far

	nSelected++;

	pi.m_Active.m_Next = true;

	if (!pi.m_Active.m_Now)
	{
		OnActive(pi, true);
		ActivatePeer(pi);
	}
}

void PeerManager::UpdateRatingsInternal(uint32_t t_ms)
{
	// calc dt in seconds, resistant to rounding
	uint32_t dt_ms = t_ms - m_TicksLast_ms;
	uint32_t dt_s = dt_ms / 1000;
	if ((t_ms % 1000) < (m_TicksLast_ms % 1000))
		dt_s++;

	uint32_t rInc = dt_s * m_Cfg.m_StarvationRatioInc;
	uint32_t rDec = dt_s * m_Cfg.m_StarvationRatioDec;

	// First unban peers
	for (RawRatingSet::reverse_iterator it = m_Ratings.rbegin(); m_Ratings.rend() != it; )
	{
		PeerInfo& pi = (it++)->get_ParentObj();
		if (pi.m_RawRating.m_Value)
			break; // starting from this - not banned

		assert(!pi.m_AdjustedRating.m_Increment); // shouldn't be adjusted while banned

		uint32_t dtThis_ms = t_ms - pi.m_LastActivity_ms;
		if (dtThis_ms >= m_Cfg.m_TimeoutBan_ms)
			ModifyRatingInternal(pi, 1, true, false);
	}

	// For inactive peers: modify adjusted ratings in-place, no need to rebuild the tree. For active (presumably lesser part) rebuild is necessary
	for (AdjustedRatingSet::iterator it = m_AdjustedRatings.begin(); m_AdjustedRatings.end() != it; )
	{
		PeerInfo& pi = (it++)->get_ParentObj();

		if (pi.m_Active.m_Now)
			m_AdjustedRatings.erase(AdjustedRatingSet::s_iterator_to(pi.m_AdjustedRating));
		else
			Rating::Inc(pi.m_AdjustedRating.m_Increment, rInc);
	}

	for (ActiveList::iterator it = m_Active.begin(); m_Active.end() != it; it++)
	{
		PeerInfo& pi = it->get_ParentObj();
		assert(pi.m_Active.m_Now);
		assert(pi.m_RawRating.m_Value); // must not be banned (i.e. must be re-inserted into m_AdjustedRatings).

		uint32_t& val = pi.m_AdjustedRating.m_Increment;
		val = (val > rDec) ? (val - rDec) : 0;
		m_AdjustedRatings.insert(pi.m_AdjustedRating);
	}
}

PeerManager::PeerInfo* PeerManager::Find(const PeerID& id, bool& bCreate)
{
	PeerInfo::ID pid;
	pid.m_Key = id;

	PeerIDSet::iterator it = m_IDs.find(pid);
	if (m_IDs.end() != it)
	{
		bCreate = false;
		return &it->get_ParentObj();
	}

	if (!bCreate)
		return NULL;

	PeerInfo* ret = AllocPeer();

	ret->m_ID.m_Key = id;
	if (!(id == Zero))
		m_IDs.insert(ret->m_ID);

	ret->m_RawRating.m_Value = Rating::Initial;
	m_Ratings.insert(ret->m_RawRating);

	ret->m_AdjustedRating.m_Increment = 0;
	m_AdjustedRatings.insert(ret->m_AdjustedRating);

	ret->m_Active.m_Now = false;
	ret->m_LastSeen = 0;
	ret->m_LastActivity_ms = 0;

	LOG_INFO() << *ret << " New";

	return ret;
}

void PeerManager::OnSeen(PeerInfo& pi)
{
	pi.m_LastSeen = getTimestamp();
}

void PeerManager::ModifyRating(PeerInfo& pi, uint32_t delta, bool bAdd)
{
	ModifyRatingInternal(pi, delta, bAdd, false);
}

void PeerManager::Ban(PeerInfo& pi)
{
	ModifyRatingInternal(pi, 0, false, true);
}

void PeerManager::ModifyRatingInternal(PeerInfo& pi, uint32_t delta, bool bAdd, bool ban)
{
	uint32_t r0 = pi.m_RawRating.m_Value;

	m_Ratings.erase(RawRatingSet::s_iterator_to(pi.m_RawRating));
	if (pi.m_RawRating.m_Value)
		m_AdjustedRatings.erase(AdjustedRatingSet::s_iterator_to(pi.m_AdjustedRating));

	if (ban)
	{
		pi.m_RawRating.m_Value = 0;
		pi.m_AdjustedRating.m_Increment = 0;
	}
	else
	{
		if (bAdd)
			Rating::Inc(pi.m_RawRating.m_Value, delta);
		else
			Rating::Dec(pi.m_RawRating.m_Value, delta);

		assert(pi.m_RawRating.m_Value);
		m_AdjustedRatings.insert(pi.m_AdjustedRating);
	}

	m_Ratings.insert(pi.m_RawRating);

	LOG_INFO() << pi << " Rating " << r0 << " -> " << pi.m_RawRating.m_Value;
}

void PeerManager::RemoveAddr(PeerInfo& pi)
{
	if (!pi.m_Addr.m_Value.empty())
	{
		m_Addr.erase(AddrSet::s_iterator_to(pi.m_Addr));
		pi.m_Addr.m_Value = io::Address();
		assert(pi.m_Addr.m_Value.empty());
	}
}

void PeerManager::ModifyAddr(PeerInfo& pi, const io::Address& addr)
{
	if (addr == pi.m_Addr.m_Value)
		return;

	LOG_INFO() << pi << " Address changed to " << addr;

	RemoveAddr(pi);

	if (addr.empty())
		return;

	PeerInfo::Addr pia;
	pia.m_Value = addr;

	AddrSet::iterator it = m_Addr.find(pia);
	if (m_Addr.end() != it)
		RemoveAddr(it->get_ParentObj());

	pi.m_Addr.m_Value = addr;
	m_Addr.insert(pi.m_Addr);
	assert(!pi.m_Addr.m_Value.empty());
}

void PeerManager::OnActive(PeerInfo& pi, bool bActive)
{
	if (pi.m_Active.m_Now != bActive)
	{
		pi.m_Active.m_Now = bActive;
		pi.m_LastActivity_ms = GetTimeNnz_ms();

		if (bActive)
			m_Active.push_back(pi.m_Active);
		else
			m_Active.erase(ActiveList::s_iterator_to(pi.m_Active));
	}
}

void PeerManager::OnRemoteError(PeerInfo& pi, bool bShouldBan)
{
	if (bShouldBan)
		Ban(pi);
	else
	{
		uint32_t dt_ms = GetTimeNnz_ms() - pi.m_LastActivity_ms;
		if (dt_ms < m_Cfg.m_TimeoutDisconnect_ms)
			ModifyRating(pi, Rating::PenaltyNetworkErr, false);
	}
}

PeerManager::PeerInfo* PeerManager::OnPeer(const PeerID& id, const io::Address& addr, bool bAddrVerified)
{
	if (id == Zero)
	{
		if (!bAddrVerified)
			return NULL;

		// find by addr
		PeerInfo::Addr pia;
		pia.m_Value = addr;

		AddrSet::iterator it = m_Addr.find(pia);
		if (m_Addr.end() != it)
			return &it->get_ParentObj();
	}

	bool bCreate = true;
	PeerInfo* pRet = Find(id, bCreate);

	if (bAddrVerified || !pRet->m_Addr.m_Value.empty() || (getTimestamp() - pRet->m_LastSeen > m_Cfg.m_TimeoutAddrChange_s))
		ModifyAddr(*pRet, addr);

	return pRet;
}

void PeerManager::Delete(PeerInfo& pi)
{
	OnActive(pi, false);
	RemoveAddr(pi);
	m_Ratings.erase(RawRatingSet::s_iterator_to(pi.m_RawRating));

	if (pi.m_RawRating.m_Value)
		m_AdjustedRatings.erase(AdjustedRatingSet::s_iterator_to(pi.m_AdjustedRating));

	if (!(pi.m_ID.m_Key == Zero))
		m_IDs.erase(PeerIDSet::s_iterator_to(pi.m_ID));

	DeletePeer(pi);
}

void PeerManager::Clear()
{
	while (!m_Ratings.empty())
		Delete(m_Ratings.begin()->get_ParentObj());

	assert(m_AdjustedRatings.empty() && m_Active.empty());
}

std::ostream& operator << (std::ostream& s, const PeerManager::PeerInfo& pi)
{
	s << "PI " << pi.m_ID.m_Key << "--" << pi.m_Addr.m_Value;
	return s;
}

} // namespace beam
