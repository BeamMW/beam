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

#include "wallet/laser/channel.h"
#include "utility/logger.h"

namespace beam::wallet::laser
{

Channel::Channel(IChannelHolder& holder,
                 const WalletID& my,
                 const WalletID& trg,
                 const Amount& fee,
                 const Amount& aMy,
                 const Amount& aTrg)
    : m_rHolder(holder)
    , m_ID(std::make_shared<ChannelID>())
    , m_widMy(my)
    , m_widTrg(trg)
    , m_aMy(aMy)
    , m_aTrg(aTrg)
{
    ECC::GenRandom(*m_ID);
    m_upReceiver = std::make_unique<Receiver>(m_rHolder, m_ID);
    m_Params.m_Fee = fee;

    Subscribe();
}
Channel::Channel(IChannelHolder& holder,
                 const ChannelIDPtr& chID,
                 const WalletID& my,
                 const WalletID& trg,
                 const Amount& fee,
                 const Amount& aMy,
                 const Amount& aTrg)
    : m_rHolder(holder)
    , m_ID(chID)
    , m_widMy(my)
    , m_widTrg(trg)
    , m_aMy(aMy)
    , m_aTrg(aTrg)
    , m_upReceiver(std::make_unique<Receiver>(holder, chID))
{
    m_Params.m_Fee = fee;
    Subscribe();
}

Channel::~Channel()
{
    Unsubscribe();
}

Height Channel::get_Tip() const
{
    Block::SystemState::Full tip;
    m_rHolder.getWalletDB()->get_History().get_Tip(tip);
    return tip.m_Height;
}

proto::FlyClient::INetwork& Channel::get_Net()
{
    return m_rHolder.get_Net();
}

Amount Channel::SelectInputs(std::vector<Key::IDV>& vInp, Amount valRequired)
{
    assert(vInp.empty());

    Amount nDone = 0;
    auto coins = m_rHolder.getWalletDB()->selectCoins(valRequired);
    vInp.reserve(coins.size());
    std::transform(coins.begin(), coins.end(), std::back_inserter(vInp),
                   [&nDone] (const Coin& coin) -> Key::IDV
                    {
                        auto idv = coin.m_ID;
                        nDone += idv.m_Value;
                        return idv;
                    });
    return nDone;
}

void Channel::get_Kdf(Key::IKdf::Ptr& pKdf)
{
    pKdf = m_rHolder.getWalletDB()->get_MasterKdf();
}

void Channel::AllocTxoID(Key::IDV& kidv)
{
    kidv.set_Subkey(0);
    kidv.m_Idx = get_RandomID();
}

void Channel::SendPeer(Negotiator::Storage::Map&& dataOut)
{
    assert(!dataOut.empty());

    if (m_SendMyWid)
    {
        m_SendMyWid = false;
        dataOut.Set(m_widMy, Codes::MyWid);
    }
    
    // BbsChannel ch;
    // m_widMy.m_Channel.Export(ch);
    // get_Net().BbsSubscribe(ch, getTimestamp(), m_upReceiver.get());

    Serializer ser;
    ser & m_ID;
    ser & Cast::Down<FieldMap>(dataOut);

    LOG_INFO() << "SendPeer\tTo peer (via bbs): " << ser.buffer().second;

    proto::FlyClient::RequestBbsMsg::Ptr pReq(new proto::FlyClient::RequestBbsMsg);
	m_widTrg.m_Channel.Export(pReq->m_Msg.m_Channel);

	ECC::NoLeak<ECC::Hash::Value> hvRandom;
	ECC::GenRandom(hvRandom.V);

	ECC::Scalar::Native nonce;
    Key::IKdf::Ptr pKdf;
    get_Kdf(pKdf);
	pKdf->DeriveKey(nonce, hvRandom.V);

	if (proto::Bbs::Encrypt(pReq->m_Msg.m_Message, m_widTrg.m_Pk, nonce, ser.buffer().first, static_cast<uint32_t>(ser.buffer().second)))
	{
		// skip mining!
		pReq->m_Msg.m_TimePosted = getTimestamp();
		get_Net().PostRequest(*pReq, *m_upReceiver);
	}
};

const ChannelIDPtr& Channel::get_chID() const
{
    return m_ID;
};

const WalletID& Channel::get_myWID() const
{
    return m_widMy;
};

const WalletID& Channel::get_trgWID() const
{
    return m_widTrg;
};

int Channel::get_lastState() const
{
    return m_LastState;
};

const Amount& Channel::get_fee() const
{
    return m_Params.m_Fee;
};

const Amount& Channel::get_amountMy() const
{
    return m_aMy;
};

const Amount& Channel::get_amountTrg() const
{
    return m_aTrg;
};

const Amount& Channel::get_amountCurrentMy() const
{
    return m_aMy;
};

const Amount& Channel::get_amountCurrentTrg() const
{
    return m_aTrg;
};

bool Channel::IsStateChanged()
{
    return m_LastState != get_State();
};

void Channel::LogNewState()
{
    beam::Lightning::Channel::State::Enum eState = get_State();
    if (m_LastState == eState)
        return;

    m_LastState = eState;

    std::ostringstream os;
    os << "State ";

    switch (eState)
    {
    case beam::Lightning::Channel::State::Opening0:
        os << "Opening0 (early stage, safe to discard)";
        break;
    case beam::Lightning::Channel::State::Opening1:
        os << "Opening1 (negotiating, no-return barrier crossed)";
        break;
    case beam::Lightning::Channel::State::Opening2:
        os << "Opening2 (Waiting channel open confirmation)";
        break;
    case beam::Lightning::Channel::State::OpenFailed:
        os << "OpenFailed (Not confirmed, missed height window). Waiting for 8 confirmations before forgetting";
        break;
    case beam::Lightning::Channel::State::Open:
        os << "Open. Last Revision: " << m_vUpdates.size() << ". Balance: " << m_vUpdates.back()->m_Outp.m_Value << " / " << (m_vUpdates.back()->m_msMy.m_Value - m_Params.m_Fee);
        break;
    case beam::Lightning::Channel::State::Updating:
        os << "Updating (creating newer Revision)";
        break;
    case beam::Lightning::Channel::State::Closing1:
        os << "Closing1 (decided to close, sent Phase-1 withdrawal)";
        break;
    case beam::Lightning::Channel::State::Closing2:
        {
            os << "Closing2 (Phase-1 withdrawal detected). Revision: " << m_State.m_Close.m_iPath << ". Initiated by " << (m_State.m_Close.m_Initiator ? "me" : "peer");
            if (DataUpdate::Type::Punishment == m_vUpdates[m_State.m_Close.m_iPath]->m_Type)
                os << ". Fraudulent withdrawal attempt detected! Will claim everything";
        }
        break;
    case beam::Lightning::Channel::State::Closed:
        os << "Closed. Waiting for 8 confirmations before forgetting";
        break;
    default:
        return;
    }

    std::cout << os.str() << std::endl;
};

void Channel::Subscribe()
{
    BbsChannel ch;
    m_widMy.m_Channel.Export(ch);
    get_Net().BbsSubscribe(ch, getTimestamp(), m_upReceiver.get());
    LOG_INFO() << "beam::wallet::laser::Channel subscribed: " << ch;
};

void Channel::Unsubscribe()
{
    BbsChannel ch;
    m_widMy.m_Channel.Export(ch);
    get_Net().BbsSubscribe(ch, 0, nullptr);
    LOG_INFO() << "beam::wallet::laser::Channel unsubscribed: " << ch;
};

}  // namespace beam::wallet::laser
