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

#include "core/lightning_codes.h"
#include "wallet/laser/channel.h"
#include "utility/logger.h"

namespace beam::wallet::laser
{

// static
ChannelIDPtr Channel::ChannelIdFromString(const std::string& chIdStr)
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
                 const Amount& aMy,
                 const Amount& aTrg,
                 const Lightning::Channel::Params& params)
    : Lightning::Channel()
    , m_rHolder(holder)
    , m_ID(std::make_shared<ChannelID>(Zero))
    , m_myAddr(myAddr)
    , m_widTrg(trg)
    , m_aMy(aMy)
    , m_aTrg(aTrg)
    , m_aCurMy(aMy)
    , m_aCurTrg(aTrg)
    , m_bbsTimestamp(getTimestamp())
{
    ECC::GenRandom(*m_ID);
    m_upReceiver = std::make_unique<Receiver>(m_rHolder, m_ID);

    m_lastState = beam::Lightning::Channel::get_State();
    m_Params = params;
}

Channel::Channel(IChannelHolder& holder,
                 const ChannelIDPtr& chID,
                 const WalletAddress& myAddr,
                 const WalletID& trg,
                 const Amount& aMy,
                 const Amount& aTrg,
                 const Lightning::Channel::Params& params)
    : Lightning::Channel()
    , m_rHolder(holder)
    , m_ID(chID)
    , m_myAddr(myAddr)
    , m_widTrg(trg)
    , m_aMy(aMy)
    , m_aTrg(aTrg)
    , m_aCurMy(aMy)
    , m_aCurTrg(aTrg)
    , m_bbsTimestamp(getTimestamp())
    , m_upReceiver(std::make_unique<Receiver>(holder, chID))
{
    m_lastState = beam::Lightning::Channel::get_State();
    m_Params = params;
}

Channel::Channel(IChannelHolder& holder,
                 const ChannelIDPtr& chID,
                 const WalletAddress& myAddr,
                 const TLaserChannelEntity& entity,
                 const Lightning::Channel::Params& params)
    : Lightning::Channel()
    , m_rHolder(holder)
    , m_ID(chID)
    , m_myAddr(myAddr)
    , m_widTrg(std::get<LaserFields::LASER_TRG_WID>(entity))
    , m_aMy(std::get<LaserFields::LASER_AMOUNT_MY>(entity))
    , m_aTrg(std::get<LaserFields::LASER_AMOUNT_TRG>(entity))
    , m_aCurMy(std::get<LaserFields::LASER_AMOUNT_CURRENT_MY>(entity))
    , m_aCurTrg(std::get<LaserFields::LASER_AMOUNT_CURRENT_TRG>(entity))
    , m_lockHeight(std::get<LaserFields::LASER_LOCK_HEIGHT>(entity))
    , m_bbsTimestamp(std::get<LaserFields::LASER_BBS_TIMESTAMP>(entity))
    , m_upReceiver(std::make_unique<Receiver>(holder, chID))
{
    m_Params = params;
    m_Params.m_Fee = std::get<LaserFields::LASER_FEE>(entity);

    RestoreInternalState(std::get<LaserFields::LASER_DATA>(entity));
    m_lastState = beam::Lightning::Channel::get_State();
}

Channel::~Channel()
{
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

Amount Channel::SelectInputs(std::vector<CoinID>& vInp, Amount valRequired, Asset::ID nAssetID)
{
    assert(vInp.empty());

    Amount nDone = 0;
    auto coins = m_rHolder.getWalletDB()->selectCoins(valRequired, nAssetID);
    vInp.reserve(coins.size());
    std::transform(coins.begin(), coins.end(), std::back_inserter(vInp),
                   [&nDone] (const Coin& coin) -> CoinID
                    {
                        const CoinID& cid = coin.m_ID;
                        nDone += cid.m_Value;
                        return cid;
                    });
    LOG_DEBUG() << "Amount selected: " << PrintableAmount(nDone, true) << " "
                << "Amount required: " << PrintableAmount(nDone, valRequired);
    return nDone;
}

void Channel::get_Kdf(Key::IKdf::Ptr& pKdf)
{
    pKdf = m_rHolder.getWalletDB()->get_MasterKdf();
    if (!pKdf)
        throw std::runtime_error("master key inaccessible");
}

void Channel::AllocTxoID(CoinID& cid)
{
    cid.set_Subkey(0);
    cid.m_Idx = get_RandomID();
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

    LOG_INFO() << "Send From: " << std::to_string(m_myAddr.m_walletID) 
               << " To peer: " << std::to_string(m_widTrg);

    proto::FlyClient::RequestBbsMsg::Ptr pReq(
        new proto::FlyClient::RequestBbsMsg);
	m_widTrg.m_Channel.Export(pReq->m_Msg.m_Channel);

	ECC::NoLeak<ECC::Hash::Value> hvRandom;
	ECC::GenRandom(hvRandom.V);

	ECC::Scalar::Native nonce;
    Key::IKdf::Ptr pKdf;
    get_Kdf(pKdf);
	pKdf->DeriveKey(nonce, hvRandom.V);

	if (proto::Bbs::Encrypt(pReq->m_Msg.m_Message,
                            m_widTrg.m_Pk,
                            nonce,
                            ser.buffer().first,
                            static_cast<uint32_t>(ser.buffer().second)))
	{
		pReq->m_Msg.m_TimePosted = getTimestamp();
		get_Net().PostRequest(*pReq, *m_upReceiver);
	}
}

void Channel::OnCoin(const CoinID& cid,
                     Height h,
                     CoinState eState,
                     bool bReverse)
{
    auto pWalletDB = m_rHolder.getWalletDB();
    auto coins = pWalletDB->getCoinsByID(std::vector<CoinID>({cid}));

    bool isNewCoin = false;
    if (coins.empty())
    {
        auto& coin = coins.emplace_back();
        coin.m_ID = cid;
        isNewCoin = true;
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


    default:
        coinStatus = Coin::Status::Unavailable;
        break;
    }

    for (auto& coin : coins)
    {
        coin.m_status = coinStatus;
        if (isNewCoin)
        {
            coin.m_maturity = h;
        }
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
    LOG_INFO() << "Coin " << cid << " " << szStatus;
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
    return m_aCurMy;
}

const Amount& Channel::get_amountCurrentTrg() const
{
    return m_aCurTrg;
}

int Channel::get_State() const
{
    return Lightning::Channel::get_State();
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

bool Channel::Open(Height hOpenTxDh)
{
    return Lightning::Channel::Open(m_aMy, m_aTrg, hOpenTxDh);
}

bool Channel::TransformLastState()
{
    auto state = get_State();
    if(state == State::Closing1 && m_gracefulClose && !m_lastUpdateStart)
        m_lastUpdateStart = get_Tip();

    if (m_lastState == state)
        return false;

    if (state == State::Updating)
    {
        m_lastUpdateStart = get_Tip();
    }
    else if (state == State::Open)
    {
        m_lastUpdateStart = 0;
    }

    m_lastState = state;
    return true;
}

int Channel::get_LastState() const
{
    return m_lastState;
}

void Channel::UpdateRestorePoint()
{
    Serializer ser;

    ser & m_State.m_hTxSentLast;
    ser & m_State.m_hQueryLast;
    ser & m_State.m_Close.m_nRevision;
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
    ser & m_iRole;
    ser & m_gracefulClose;
    ser & m_lastUpdateStart;
    ser & m_pOpen->m_vInp.size();
    for (const CoinID& cid : m_pOpen->m_vInp)
    {
        ser & cid;
    }
    ser & m_pOpen->m_cidChange;

    ser & m_nRevision;
    ser & m_lstUpdates.size();
    for (const auto& upd : m_lstUpdates)
    {
        ser & upd.m_Comm1;
        ser & upd.m_tx1;
        ser & upd.m_tx2;
        ser & upd.m_CommPeer1;
        ser & upd.m_txPeer2;
        ser & upd.m_hvTx1KernelID;

        ser & upd.m_RevealedSelfKey;
        ser & upd.m_PeerKeyValid;
        ser & upd.m_PeerKey;

        ser & upd.m_msMy;
        ser & upd.m_msPeer;
        ser & upd.m_Outp;
        ser & upd.m_Type;
    }

    Blob blob(ser.buffer().first, static_cast<uint32_t>(ser.buffer().second));
    blob.Export(m_data);

    m_bbsTimestamp = getTimestamp();

    if (!m_lstUpdates.empty())
    {
        const auto& lastUpdate = m_lstUpdates.back();
        m_aCurMy = lastUpdate.m_Outp.m_Value;
        auto total = lastUpdate.m_msMy.m_Value;
        if (!m_gracefulClose)
        {
            total -= m_Params.m_Fee;
        }
        m_aCurTrg = total - m_aCurMy;

        const HeightRange* pHR = lastUpdate.get_HR();
        m_lockHeight = pHR ? pHR->m_Max - m_Params.m_hLockTime - m_Params.m_hPostLockReserve : MaxHeight;
    }
    else
    {
        if (m_pOpen && m_pOpen->m_hOpened)
        {
            m_lockHeight =
                  m_pOpen->m_hOpened
                + m_Params.m_hRevisionMaxLifeTime
                - m_Params.m_hLockTime
                - m_Params.m_hPostLockReserve;
        }
    }
    
}

void Channel::LogState()
{
    std::ostringstream os;
    os << "Channel:" << to_hex(m_ID->m_pData, m_ID->nBytes) << " state ";

    switch (get_State())
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
        os << "OpenFailed (Not confirmed, missed height window). Waiting for " << Rules::get().MaxRollback << " confirmations before forgetting";
        break;
    case beam::Lightning::Channel::State::Open:
        os << "Open. Last Revision: " << m_nRevision
           << ". My balance: " << m_lstUpdates.back().m_Outp.m_Value
           << " / Total balance: " << (m_lstUpdates.back().m_msMy.m_Value - m_Params.m_Fee);
        break;
    case beam::Lightning::Channel::State::Updating:
        os << "Updating (creating newer Revision)";
        break;
    case beam::Lightning::Channel::State::Closing1:
        os << "Closing1 (decided to close, sent Phase-1 withdrawal)";
        break;
    case beam::Lightning::Channel::State::Closing2:
        {
            os << "Closing2 (Phase-1 withdrawal detected). Revision: "
               << m_State.m_Close.m_nRevision << ". Initiated by " 
               << (m_State.m_Close.m_Initiator ? "me" : "peer");
            if (DataUpdate::Type::Punishment == m_State.m_Close.m_pPath->m_Type)
            {
                os << ". Fraudulent withdrawal attempt detected! Will claim everything";
            }
        }
        break;
    case beam::Lightning::Channel::State::Closed:
        os << "Closed. Waiting for " << Rules::get().MaxRollback << " confirmations before forgetting";
        break;
    case beam::Lightning::Channel::State::Expired:
        os << "Expired (you can delete this channel)";
        break;
    default:
        return;
    }

    LOG_DEBUG() << os.str();
}

void Channel::Subscribe()
{
    BbsChannel ch;
    get_myWID().m_Channel.Export(ch);
    get_Net().BbsSubscribe(ch, m_bbsTimestamp, m_upReceiver.get());
    m_isSubscribed = true;
    LOG_INFO() << "beam::wallet::laser::Channel WalletID: "  << std::to_string(get_myWID()) << " subscribes to BBS channel: " << ch;
}

void Channel::Unsubscribe()
{
    BbsChannel ch;
    get_myWID().m_Channel.Export(ch);
    get_Net().BbsSubscribe(ch, 0, nullptr);
    m_isSubscribed = false;
    if (!m_lastUpdateStart && m_gracefulClose)
        m_lastUpdateStart = get_Tip();
    LOG_INFO() << "beam::wallet::laser::Channel WalletID: "  << std::to_string(get_myWID()) << " unsubscribed from BBS channel: " << ch;
    
}

bool Channel::IsSafeToClose() const
{
    if (!m_pOpen)
        return true;

    if (!m_pOpen->m_hOpened)
    {
        return
            m_State.m_hQueryLast >= m_pOpen->m_hrLimit.m_Max ||
            DataUpdate::Type::None == m_lstUpdates.front().m_Type;
    }

    return 
        m_State.m_Close.m_hPhase2 && m_State.m_Close.m_hPhase2 <= get_Tip();
}

bool Channel::IsUpdateStuck() const
{
    return m_lastUpdateStart && (m_lastUpdateStart + Lightning::kMaxBlackoutTime < get_Tip());
}

bool Channel::IsGracefulCloseStuck() const
{
    return m_gracefulClose && !m_State.m_Terminate && IsUpdateStuck();
}

bool Channel::IsSubscribed() const
{
    return m_isSubscribed;
}

void Channel::RestoreInternalState(const ByteBuffer& data)
{
    try
    {
        Deserializer der;
        der.reset(data.data(), data.size());

        der & m_State.m_hTxSentLast;
        der & m_State.m_hQueryLast;
        der & m_State.m_Close.m_nRevision;
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
        der & m_iRole;
        der & m_gracefulClose;
        der & m_lastUpdateStart;
        if (m_gracefulClose)
            m_lastUpdateStart = 0;

        size_t vInpSize = 0;
        der & vInpSize;
        m_pOpen->m_vInp.reserve(vInpSize);
        for (size_t i = 0; i < vInpSize; ++i)
        {
            CoinID cid;
            der & cid;
            m_pOpen->m_vInp.push_back(cid);
        }
        der & m_pOpen->m_cidChange;

        der & m_nRevision;
        size_t vUpdatesSize = 0;
        der & vUpdatesSize;
        for (size_t i = 0; i < vUpdatesSize; ++i)
        {
            DataUpdate* pVal = new DataUpdate;
            DataUpdate& upd = *pVal;
            m_lstUpdates.push_back(upd);

            der & upd.m_Comm1;
            der & upd.m_tx1;
            der & upd.m_tx2;
            der & upd.m_CommPeer1;
            der & upd.m_txPeer2;
            der & upd.m_hvTx1KernelID;

            der & upd.m_RevealedSelfKey;
            der & upd.m_PeerKeyValid;
            der & upd.m_PeerKey;

            der & upd.m_msMy;
            der & upd.m_msPeer;
            der & upd.m_Outp;
            der & upd.m_Type;

            if (m_State.m_Close.m_nRevision == m_nRevision - vUpdatesSize + i + 1)
                m_State.m_Close.m_pPath = pVal;
        }
    }
    catch (const std::exception&)
    {
		LOG_ERROR() << "RestoreInternalState failed";
	}

    m_SendMyWid = false;
}

}  // namespace beam::wallet::laser
