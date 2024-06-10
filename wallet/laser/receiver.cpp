// Copyright 2019 The Beam Team
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

#include "wallet/laser/receiver.h"
#include "utility/logger.h"

namespace beam::wallet::laser
{
Receiver::Receiver(IReceiverHolder& holder, const ChannelIDPtr& chID)
    : m_rHolder(holder), m_chID(chID)
{
}

Receiver::~Receiver()
{
}

void Receiver::OnComplete(proto::FlyClient::Request&)
{
    BEAM_LOG_DEBUG() << "Receiver::OnComplete";
}

void Receiver::OnMsg(proto::BbsMsg&& msg)
{
    if (msg.m_Message.empty())
		return;

	Blob blob;

	uint8_t* pMsg = &msg.m_Message.front();
	blob.n = static_cast<uint32_t>(msg.m_Message.size());

    if (!m_rHolder.Decrypt(m_chID, pMsg, &blob))
        return;

    m_rHolder.OnMsg(m_chID, std::move(blob));
}

}  // namespace beam::wallet::laser
