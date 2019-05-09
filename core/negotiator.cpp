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

#include "negotiator.h"
#include "ecc_native.h"

namespace beam {
namespace Negotiator {

namespace Gateway
{
	void Direct::Send(uint32_t code, ByteBuffer&& buf)
	{
		const uint32_t msk = (uint32_t(1) << 16) - 1;
		if ((code & msk) >= Codes::Private)
		{
			assert(false);
			return;
		}

		Blob blob;
		if (m_Peer.m_pStorage->Read(code, blob))
		{
			assert(false);
			return;
		}

		m_Peer.m_pStorage->Send(code, std::move(buf));
	}


} // namespace Gateway

namespace Storage
{

	void Map::Send(uint32_t code, ByteBuffer&& buf)
	{
		operator [](code) = std::move(buf);
	}

	bool Map::Read(uint32_t code, Blob& blob)
	{
		const_iterator it = find(code);
		if (end() == it)
			return false;

		blob = Blob(it->second);
		return true;
	}

} // namespace Storage

/////////////////////
// IBase

void IBase::OnFail()
{
	Set(Status::Error, Codes::Status);
}

void IBase::OnDone()
{
	Set(Status::Success, Codes::Status);
}

bool IBase::RaiseTo(uint32_t pos)
{
	if (m_Pos >= pos)
		return false;

	m_Pos = pos;
	return true;
}

uint32_t IBase::Update()
{
	uint32_t nStatus = Status::Pending;
	if (Get(nStatus, Codes::Status))
		return nStatus;

	uint32_t nPos = 0;
	Get(nPos, Codes::Position);
	m_Pos = nPos;

	Update2();

	if (Get(nStatus, Codes::Status) && (nStatus > Status::Success))
		return nStatus;

	assert(m_Pos >= nPos);
	if (m_Pos > nPos)
		Set(m_Pos, Codes::Position);

	return nStatus;
}

} // namespace Negotiator
} // namespace beam
