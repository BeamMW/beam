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

// static
ChannelIDPtr Channel::ChIdFromString(const std::string& chIdStr)
{
    bool isValid = false;
    auto buffer = from_hex(chIdStr, &isValid);

    if (isValid)
    {
        auto chId = std::make_shared<ChannelID>(Zero);
        memcpy(&(chId->m_pData), buffer.data(), buffer.size());

        return chId;
    }
    return nullptr;
}

Channel::Channel(IChannelHolder& holder,
                 const WalletAddress& myAddr,
                 const WalletID& trg,
                 const Amount& fee,
                 const Amount& aMy,
                 const Amount& aTrg,
                 Height locktime)
    : Lightning::Channel()
    , m_rHolder(holder)
    , m_ID(std::make_shared<ChannelID>(Zero))
    , m_myAddr(myAddr)
    , m_widTrg(trg)
    , m_aMy(aMy)
    , m_aTrg(aTrg)
    , m_bbsTimestamp(getTimestamp())
{
    ECC::GenRandom(*m_ID);
    m_upReceiver = std::make_unique<Receiver>(m_rHolder, m_ID);
    m_Params.m_Fee = fee;
    m_Params.m_hLockTime = locktime;

    Subscribe();
}

Channel::Channel(IChannelHolder& holder,
                 const ChannelIDPtr& chID,
                 const WalletAddress& myAddr,
                 const WalletID& trg,
                 const Amount& fee,
                 const Amount& aMy,
                 const Amount& aTrg,
                 Height locktime)
    : Lightning::Channel()
    , m_rHolder(holder)
    , m_ID(chID)
    , m_myAddr(myAddr)
    , m_widTrg(trg)
    , m_aMy(aMy)
    , m_aTrg(aTrg)
    , m_bbsTimestamp(getTimestamp())
    , m_upReceiver(std::make_unique<Receiver>(holder, chID))
{
    m_Params.m_Fee = fee;
    m_Params.m_hLockTime = locktime;

    Subscribe();
}

Channel::Channel(IChannelHolder& holder,
                 const ChannelIDPtr& chID,
                 const WalletAddress& myAddr,
                 const TLaserChannelEntity& entity)
    : Lightning::Channel()
    , m_rHolder(holder)
    , m_ID(chID)
    , m_myAddr(myAddr)
    , m_widTrg(std::get<2>(entity))
    , m_aMy(std::get<6>(entity))
    , m_aTrg(std::get<7>(entity))
    , m_lockHeight(std::get<10>(entity))
    , m_bbsTimestamp(std::get<11>(entity))
    , m_upReceiver(std::make_unique<Receiver>(holder, chID))
{
    m_Params.m_Fee = std::get<4>(entity);
    m_Params.m_hLockTime = std::get<5>(entity);

    RestoreInternalState(std::get<12>(entity));

    if (get_State() != beam::Lightning::Channel::State::OpenFailed ||
        get_State() != beam::Lightning::Channel::State::Closed)
    {
        Subscribe(); 
    }
}

// Channel::Channel(Channel&& channel)
// { 
//     LOG_INFO() << "Channel(Channel&& channel)";
// }

// void  Channel::operator=(Channel&& channel)
// {
//     LOG_INFO() << "operator=(Channel&& channel)";
// }

Channel::~Channel()
{
    Unsubscribe();
}

Height Channel::get_Tip() const
{
    Block::SystemState::Full tip;
    auto& history = m_rHolder.getWalletDB()->get_History();
    history.get_Tip(tip);
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
        dataOut.Set(m_myAddr.m_walletID, Codes::MyWid);
    }
    
    Serializer ser;
    ser & (*m_ID);
    ser & Cast::Down<FieldMap>(dataOut);

    LOG_INFO() << "LASER Send From: " << std::to_string(m_myAddr.m_walletID) << " To peer " << std::to_string(m_widTrg);

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
}

void Channel::OnCoin(const ECC::Key::IDV& kidv,
                     Height h,
                     CoinState eState,
                     bool bReverse)
{
    auto pWalletDB = m_rHolder.getWalletDB();
    auto coins = pWalletDB->getCoinsByID(std::vector<ECC::Key::IDV>({kidv}));
    if (coins.empty())
    {
        auto& coin = coins.emplace_back();
        coin.m_ID = kidv;
        coin.m_maturity = m_pOpen && m_pOpen->m_hOpened
            ? m_Params.m_hLockTime + m_pOpen->m_hOpened
            : m_Params.m_hLockTime + h;
    }
    
    const char* szStatus = "";
    Coin::Status coinStatus = Coin::Status::Unavailable;
    switch (eState)
    {
    case CoinState::Locked:
        szStatus = bReverse ? "Unlocked" : "Locked";
        if (bReverse)
        {
            szStatus = "Unlocked";
            coinStatus = Coin::Status::Available;
        }
        else
        {
            szStatus = "Locked";
            coinStatus = Coin::Status::Outgoing;
        }
        break;

    case CoinState::Spent:
        if (bReverse)
        {
            szStatus = "Unspent";
            coinStatus = Coin::Status::Available;
        }
        else
        {
            szStatus = "Spent";
            coinStatus = Coin::Status::Spent;
        }
        break;

    case CoinState::Confirmed:
        szStatus = bReverse ? "Unconfirmed" : "Confirmed";
        if (bReverse)
        {
            szStatus = "Unconfirmed";
            coinStatus = Coin::Status::Incoming;
        }
        else
        {
            szStatus = "Confirmed";
            coinStatus = Coin::Status::Available;
        }
        break;


    default: // suppress warning
        coinStatus = Coin::Status::Unavailable;
        break;
    }

    for (auto& coin : coins)
    {
        coin.m_status = coinStatus;
        switch(coinStatus)
        {
        case Coin::Status::Available:
            coin.m_confirmHeight = h;
            break;
        case Coin::Status::Spent:
            coin.m_spentHeight = h;
            break;
        default:
            break;
        }
    }

    pWalletDB->saveCoins(coins);
    LOG_INFO() << "LASER Coin " << kidv.m_Value << " " << szStatus;
}

const ChannelIDPtr& Channel::get_chID() const
{
    return m_ID;
}

const WalletID& Channel::get_myWID() const
{
    return m_myAddr.m_walletID;
}

const WalletID& Channel::get_trgWID() const
{
    return m_widTrg;
}

const Amount& Channel::get_fee() const
{
    return m_Params.m_Fee;
}

const Height& Channel::getLocktime() const
{
    return m_Params.m_hLockTime;
}

const Amount& Channel::get_amountMy() const
{
    return m_aMy;
}

const Amount& Channel::get_amountTrg() const
{
    return m_aTrg;
}

const Amount& Channel::get_amountCurrentMy() const
{
    return m_aMy;
}

const Amount& Channel::get_amountCurrentTrg() const
{
    return m_aTrg;
}

int Channel::get_State() const
{
    return beam::Lightning::Channel::get_State();
}

const Height& Channel::get_LockHeight() const
{
    return m_lockHeight;
}

const Timestamp& Channel::get_BbsTimestamp() const
{
    return m_bbsTimestamp;
}

const ByteBuffer& Channel::get_Data() const
{
    return m_data;
}

const WalletAddress& Channel::get_myAddr() const
{
    return m_myAddr;
}

bool Channel::Open(HeightRange openWindow)
{
    return Lightning::Channel::Open(m_aMy, m_aTrg, openWindow);
}

bool Channel::IsStateChanged()
{
    return m_LastState != beam::Lightning::Channel::get_State();
}

void Channel::UpdateRestorePoint()
{
    Serializer ser;

    ser & m_State.m_hTxSentLast;
    ser & m_State.m_hQueryLast;
    ser & m_State.m_Close.m_iPath;
    ser & m_State.m_Close.m_Initiator;
    ser & m_State.m_Close.m_hPhase1;
    ser & m_State.m_Close.m_hPhase2;
    ser & m_State.m_Terminate;

    ser & m_pOpen->m_Comm0;
    ser & m_pOpen->m_ms0;
    ser & m_pOpen->m_hrLimit.m_Min;
    ser & m_pOpen->m_hrLimit.m_Max;
    ser & m_pOpen->m_txOpen;
    ser & m_pOpen->m_hvKernel0;
    ser & m_pOpen->m_hOpened;
    ser & m_pOpen->m_vInp.size();
    for (auto& inp : m_pOpen->m_vInp)
    {
        ser & inp;
    }
    ser & m_pOpen->m_kidvChange;

    ser & m_vUpdates.size();
    for (auto& upd : m_vUpdates)
    {
        ser & upd->m_Comm1;
        ser & upd->m_tx1;
        ser & upd->m_tx2;
        ser & upd->m_CommPeer1;
        ser & upd->m_txPeer2;

        ser & upd->m_RevealedSelfKey;
        ser & upd->m_PeerKeyValid;
        ser & upd->m_PeerKey;

        ser & upd->m_msMy;
        ser & upd->m_msPeer;
        ser & upd->m_Outp;
        ser & upd->m_Type;
    }

    Blob blob(ser.buffer().first, static_cast<uint32_t>(ser.buffer().second));
    blob.Export(m_data);
}

void Channel::LogNewState()
{
    beam::Lightning::Channel::State::Enum eState =
        beam::Lightning::Channel::get_State();
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
}

// bool Channel::IsOpen() const
// {
//     return beam::Lightning::Channel::get_State() ==
//            beam::Lightning::Channel::State::Open;
// };

void Channel::Subscribe()
{
    BbsChannel ch;
    get_myWID().m_Channel.Export(ch);
    get_Net().BbsSubscribe(ch, getTimestamp(), m_upReceiver.get());
    LOG_INFO() << "beam::wallet::laser::Channel subscribed: " << ch;
}

void Channel::Unsubscribe()
{
    BbsChannel ch;
    get_myWID().m_Channel.Export(ch);
    get_Net().BbsSubscribe(ch, 0, nullptr);
    LOG_INFO() << "beam::wallet::laser::Channel unsubscribed: " << ch;
}

void Channel::RestoreInternalState(const ByteBuffer& data)
{
    try
    {
        Deserializer der;
        der.reset(data.data(), data.size());

        der & m_State.m_hTxSentLast;
        der & m_State.m_hQueryLast;
        der & m_State.m_Close.m_iPath;
        der & m_State.m_Close.m_Initiator;
        der & m_State.m_Close.m_hPhase1;
        der & m_State.m_Close.m_hPhase2;
        der & m_State.m_Terminate;

        m_pOpen = std::make_unique<DataOpen>();
        der & m_pOpen->m_Comm0;
        der & m_pOpen->m_ms0;
        der & m_pOpen->m_hrLimit.m_Min;
        der & m_pOpen->m_hrLimit.m_Max;
        der & m_pOpen->m_txOpen;
        der & m_pOpen->m_hvKernel0;
        der & m_pOpen->m_hOpened;

        size_t vInpSize = 0;
        der & vInpSize;
        m_pOpen->m_vInp.reserve(vInpSize);
        for (auto i = 0; i < vInpSize; ++i)
        {
            Key::IDV idv;
            der & idv;
            m_pOpen->m_vInp.push_back(idv);
        }
        der & m_pOpen->m_kidvChange;

        size_t vUpdatesSize = 0;
        der & vUpdatesSize;
        m_vUpdates.reserve(vUpdatesSize);
        for (auto i = 0; i < vUpdatesSize; ++i)
        {
            auto& upd = m_vUpdates.emplace_back(std::make_unique<DataUpdate>());
            der & upd->m_Comm1;
            der & upd->m_tx1;
            der & upd->m_tx2;
            der & upd->m_CommPeer1;
            der & upd->m_txPeer2;

            der & upd->m_RevealedSelfKey;
            der & upd->m_PeerKeyValid;
            der & upd->m_PeerKey;

            der & upd->m_msMy;
            der & upd->m_msPeer;
            der & upd->m_Outp;
            der & upd->m_Type;
        }
    }
    catch (const std::exception&)
    {
		LOG_ERROR() << "LASER RestoreInternalState failed";
	}

    m_SendMyWid = false;
}

}  // namespace beam::wallet::laser
