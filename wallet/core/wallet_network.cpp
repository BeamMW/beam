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

#include "wallet_network.h"

using namespace std;

namespace
{
    const char* BBS_TIMESTAMPS = "BbsTimestamps";
    const unsigned AddressUpdateInterval_ms = 60 * 1000; // check addresses every minute
}


namespace beam::wallet {

    ///////////////////////////

    BaseMessageEndpoint::BaseMessageEndpoint(IWalletMessageConsumer& w, const IWalletDB::Ptr& pWalletDB)
        : m_Wallet(w)
        , m_WalletDB(pWalletDB)
        , m_pKdfSbbs(pWalletDB->get_SbbsKdf())
        , m_AddressExpirationTimer(io::Timer::create(io::Reactor::get_Current()))
    {

    }

    BaseMessageEndpoint::~BaseMessageEndpoint()
    {
        
    }

    void BaseMessageEndpoint::Subscribe()
    {
        auto myAddresses = m_WalletDB->getAddresses(true);
        for (const auto& address : myAddresses)
            if (!address.isExpired())
                AddOwnAddress(address);

        m_AddressExpirationTimer->start(AddressUpdateInterval_ms, false, [this] { OnAddressTimer(); });
    }

    void BaseMessageEndpoint::Unsubscribe()
    {
        while (!m_Addresses.empty())
            DeleteAddr(m_Addresses.begin()->get_ParentObj());
    }

    void BaseMessageEndpoint::ProcessMessage(BbsChannel channel, const ByteBuffer& msg)
    {
        Addr::Channel key;
        key.m_Value = channel;

        for (ChannelSet::iterator it = m_Channels.lower_bound(key); ; ++it)
        {
            if (m_Channels.end() == it)
                break;
            if (it->m_Value != channel)
                break; // as well


            if (!m_pKdfSbbs)
            {
                // read-only wallet
                m_WalletDB->saveIncomingWalletMessage(channel, msg);
                OnIncomingMessage();
                return;
            }

            ByteBuffer buf = msg; // duplicate
            uint8_t* pMsg = &buf.front();
            uint32_t nSize = static_cast<uint32_t>(buf.size());

            auto& x = it->get_ParentObj();

            if (!proto::Bbs::Decrypt(pMsg, nSize, x.m_sk))
                continue;

            if (x.m_pHandler)
                x.m_pHandler->OnMsg(Blob(pMsg, nSize));
            else
            {
                SetTxParameter msgWallet;
                bool bValid = false;

                try {
                    Deserializer der;
                    der.reset(pMsg, nSize);
                    der& msgWallet;
                    bValid = true;
                }
                catch (const std::exception&) {
                    LOG_WARNING() << "BBS deserialization failed";
                }

                if (bValid)
                {
                    m_Wallet.OnWalletMessage(it->get_ParentObj().m_Wid.m_Value, msgWallet);
                    break;
                }
            }
        }
    }

    BaseMessageEndpoint::Addr* BaseMessageEndpoint::CreateOwnAddr(const WalletID& wid)
    {
        Addr* pAddr = new Addr;
        pAddr->m_Wid.m_Value = wid;
        pAddr->m_Channel.m_Value = wid.get_Channel();

        m_Addresses.insert(pAddr->m_Wid);
        m_Channels.insert(pAddr->m_Channel);

        if (IsSingleChannelUser(pAddr->m_Channel))
            OnChannelAdded(pAddr->m_Channel.m_Value);

        return pAddr;
    }

    void BaseMessageEndpoint::AddOwnAddress(const WalletAddress& address)
    {
        if (!m_pKdfSbbs)
            return;

        Addr::Wid key;
        key.m_Value = address.m_walletID;

        Addr* pAddr = nullptr;
        auto itW = m_Addresses.find(key);

        if (m_Addresses.end() == itW)
        {
            pAddr = CreateOwnAddr(address.m_walletID);
            m_WalletDB->get_SbbsPeerID(pAddr->m_sk, pAddr->m_Wid.m_Value.m_Pk, address.m_OwnID);
        }
        else
        {
            pAddr = &(itW->get_ParentObj());
        }

        pAddr->m_ExpirationTime = address.getExpirationTime();

        LOG_INFO() << "WalletID " << to_string(address.m_walletID) << " subscribes to BBS channel " << pAddr->m_Channel.m_Value;
    }

    void BaseMessageEndpoint::DeleteOwnAddress(const WalletID& wid)
    {
        Addr::Wid key;
        key.m_Value = wid;

        auto it = m_Addresses.find(key);
        if (m_Addresses.end() != it)
            DeleteAddr(it->get_ParentObj());
    }

    void BaseMessageEndpoint::DeleteAddr(const Addr& v)
    {
        if (IsSingleChannelUser(v.m_Channel))
        {
            OnChannelDeleted(v.m_Channel.m_Value);
        }

        m_Addresses.erase(WidSet::s_iterator_to(v.m_Wid));
        m_Channels.erase(ChannelSet::s_iterator_to(v.m_Channel));
        delete& v;
    }

    bool BaseMessageEndpoint::IsSingleChannelUser(const Addr::Channel& c)
    {
        ChannelSet::const_iterator it = ChannelSet::s_iterator_to(c);
        ChannelSet::const_iterator it2 = it;
        if (((++it2) != m_Channels.end()) && (it2->m_Value == c.m_Value))
            return false;

        if (it != m_Channels.begin())
        {
            it2 = it;
            if ((--it2)->m_Value == it->m_Value)
                return false;
        }

        return true;
    }

    void BaseMessageEndpoint::Send(const WalletID& peerID, const SetTxParameter& msg)
    {
        if (!m_pKdfSbbs)
            return;

        Serializer ser;
        ser & msg;
        SerializeBuffer sb = ser.buffer();

        Send(peerID, Blob(sb.first, static_cast<uint32_t>(sb.second)));
    }

    void BaseMessageEndpoint::Send(const WalletID& peerID, const Blob& msg)
    {
        if (!m_pKdfSbbs)
            return;

        ECC::NoLeak<ECC::Hash::Value> hvRandom;
        ECC::GenRandom(hvRandom.V);

        ECC::Scalar::Native nonce;
        m_pKdfSbbs->DeriveKey(nonce, hvRandom.V);

        ByteBuffer encryptedMessage;
        if (proto::Bbs::Encrypt(encryptedMessage, peerID.m_Pk, nonce, msg.p, msg.n))
        {
            SendRawMessage(peerID, encryptedMessage);
        }
        else
        {
            LOG_WARNING() << "BBS serialization failed (bad peerID?)";
        }
    }

    void BaseMessageEndpoint::OnAddressTimer()
    {
        vector<Addr*> addressesToDelete;
        for (const auto& address : m_Addresses)
        {
            if (address.get_ParentObj().IsExpired())
            {
                addressesToDelete.push_back(&address.get_ParentObj());
            }
        }
        for (const auto& address : addressesToDelete)
        {
            DeleteAddr(*address);
        }
        m_AddressExpirationTimer->start(AddressUpdateInterval_ms, false, [this] { OnAddressTimer(); });
    }

    void BaseMessageEndpoint::Listen(const WalletID& addr, const ECC::Scalar::Native& sk, IHandler* pHandler)
    {
        Addr* pAddr = CreateOwnAddr(addr);
        pAddr->m_sk = sk;
        pAddr->m_ExpirationTime = Timestamp(-1);
        pAddr->m_pHandler = pHandler;
    }

    void BaseMessageEndpoint::Unlisten(const WalletID& wid)
    {
        DeleteOwnAddress(wid);
    }

    /////////////////////////
    TimestampHolder::TimestampHolder(IWalletDB::Ptr walletDB, const char* timestampsKey)
        : m_WalletDB(walletDB)
        , m_TimestampsKey(timestampsKey)
    {
        storage::getBlobVar(*m_WalletDB, m_TimestampsKey.data(), m_BbsTimestamps);
    }
    TimestampHolder::~TimestampHolder()
    {
        SaveBbsTimestamps();
    }

    Timestamp TimestampHolder::GetTimestamp(BbsChannel channel)
    {
        Timestamp ts = 0;
        auto it = m_BbsTimestamps.find(channel);
        if (m_BbsTimestamps.end() != it)
            ts = it->second;
        return ts;
    }

    void TimestampHolder::UpdateTimestamp(const proto::BbsMsg& msg)
    {
        auto itBbs = m_BbsTimestamps.find(msg.m_Channel);
        if (m_BbsTimestamps.end() != itBbs)
        {
            std::setmax(itBbs->second, msg.m_TimePosted);
        }
        else
        {
            m_BbsTimestamps[msg.m_Channel] = msg.m_TimePosted;
        }

        if (!m_pTimerBbsTmSave)
        {
            m_pTimerBbsTmSave = io::Timer::create(io::Reactor::get_Current());
            m_pTimerBbsTmSave->start(60 * 1000, false, [this]() { OnTimerBbsTmSave(); });
        }
    }

    void TimestampHolder::SaveBbsTimestamps()
    {
        Timestamp tsThreshold = getTimestamp() - 3600 * 24 * 3;

        for (auto it = m_BbsTimestamps.begin(); m_BbsTimestamps.end() != it; )
        {
            auto it2 = it++;
            if (it2->second < tsThreshold)
                m_BbsTimestamps.erase(it2);
        }
        storage::setBlobVar(*m_WalletDB, m_TimestampsKey.data(), m_BbsTimestamps);
    }

    void TimestampHolder::OnTimerBbsTmSave()
    {
        m_pTimerBbsTmSave.reset();
        SaveBbsTimestamps();
    }


    ///////////////////////////

    BbsProcessor::BbsProcessor(proto::FlyClient::INetwork::Ptr nodeEndpoint, ITimestampHolder::Ptr timestampHolder)
        : m_NodeEndpoint(nodeEndpoint)
        , m_TimestampHolder(timestampHolder)
    {
    }

    BbsProcessor::~BbsProcessor()
    {
        try
        {
            m_Miner.Stop();
            while (!m_PendingBbsMsgs.empty())
                DeleteReq(m_PendingBbsMsgs.front());
        }
        catch (const std::exception & e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...)
        {
            LOG_UNHANDLED_EXCEPTION();
        }
    }

    void BbsProcessor::Send(const WalletID& peerID, const ByteBuffer& msg, uint64_t messageID)
    {
        BbsMiner::Task::Ptr pTask = std::make_shared<BbsMiner::Task>();
        pTask->m_Msg.m_Message = msg;

        pTask->m_Done = false;
        pTask->m_Msg.m_Channel = peerID.get_Channel();

        pTask->m_StoredMessageID = messageID; // store id to be able to remove if send succeeded

        if (m_MineOutgoing && !Rules::get().FakePoW)
        {
            proto::Bbs::get_HashPartial(pTask->m_hpPartial, pTask->m_Msg);

            if (!m_Miner.m_pEvt)
            {
                m_Miner.m_pEvt = io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { OnMined(); });
                m_Miner.m_Shutdown = false;
#if defined(EMSCRIPTEN)
                uint32_t nThreads = 3;
#else

                uint32_t nThreads = MyThread::hardware_concurrency();
#endif
                nThreads = (nThreads > 1) ? (nThreads - 1) : 1; // leave at least 1 vacant core for other things
                m_Miner.m_vThreads.resize(nThreads);

                for (uint32_t i = 0; i < nThreads; i++)
                    m_Miner.m_vThreads[i] = MyThread(&BbsMiner::Thread, &m_Miner, i, Rules::get());
            }

            std::unique_lock<std::mutex> scope(m_Miner.m_Mutex);

            m_Miner.m_Pending.push_back(std::move(pTask));
            m_Miner.m_NewTask.notify_all();
        }
        else
        {
            pTask->m_Msg.m_TimePosted = getTimestamp();
            OnMined(pTask);
        }
    }

    proto::FlyClient::IBbsReceiver* BbsProcessor::get_BbsReceiver()
    {
        return &m_BbsSentEvt;
    }

    void BbsProcessor::OnMsgImpl(const proto::BbsMsg& msg)
    {
        if (msg.m_Message.empty())
            return;

        m_TimestampHolder->UpdateTimestamp(msg);

        OnMsg(msg);
    }

    void BbsProcessor::DeleteReq(WalletRequestBbsMsg& r)
    {
        m_PendingBbsMsgs.erase(BbsMsgList::s_iterator_to(r));
        r.m_pTrg = NULL;
        OnMessageSent(r.m_MessageID);
        r.Release();
    }

    void BbsProcessor::BbsSentEvt::OnComplete(proto::FlyClient::Request& r)
    {
        assert(r.get_Type() == proto::FlyClient::Request::Type::BbsMsg);
        get_ParentObj().DeleteReq(static_cast<WalletRequestBbsMsg&>(r));
    }

    void BbsProcessor::BbsSentEvt::OnMsg(proto::BbsMsg&& msg)
    {
        get_ParentObj().OnMsgImpl(msg);
    }

    void BbsProcessor::SubscribeChannel(BbsChannel channel)
    {
        m_NodeEndpoint->BbsSubscribe(channel, m_TimestampHolder->GetTimestamp(channel), get_BbsReceiver());
    }

    void BbsProcessor::UnsubscribeChannel(BbsChannel channel)
    {
        m_NodeEndpoint->BbsSubscribe(channel, 0, nullptr);
    }

    void BbsProcessor::OnMined()
    {
        while (true)
        {
            BbsMiner::Task::Ptr pTask;
            {
                std::unique_lock<std::mutex> scope(m_Miner.m_Mutex);

                if (!m_Miner.m_Done.empty())
                {
                    pTask = std::move(m_Miner.m_Done.front());
                    m_Miner.m_Done.pop_front();
                }
            }

            if (!pTask)
                break;

            OnMined(pTask);
        }
    }

    void BbsProcessor::OnMined(BbsMiner::Task::Ptr task)
    {
        WalletRequestBbsMsg::Ptr pReq(new WalletRequestBbsMsg);

        pReq->m_Msg = std::move(task->m_Msg);
        pReq->m_MessageID = task->m_StoredMessageID;

        m_PendingBbsMsgs.push_back(*pReq);
        pReq->AddRef();

        m_NodeEndpoint->PostRequest(*pReq, m_BbsSentEvt);
    }

    ///////////////////////////

    WalletNetworkViaBbs::WalletNetworkViaBbs(IWalletMessageConsumer& w, proto::FlyClient::INetwork::Ptr net, const IWalletDB::Ptr& pWalletDB)
        : BaseMessageEndpoint(w, pWalletDB)
        , BbsProcessor(net, std::make_shared<TimestampHolder>(pWalletDB, BBS_TIMESTAMPS))
        , m_WalletDB(pWalletDB)
    {
        Subscribe();
        m_WalletDB->Subscribe(this);
	}

	WalletNetworkViaBbs::~WalletNetworkViaBbs()
	{
        try 
        {
            m_WalletDB->Unsubscribe(this);

            Unsubscribe();

		} 
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...)
        {
            LOG_UNHANDLED_EXCEPTION();
		}
	}

	void WalletNetworkViaBbs::OnMsg(const proto::BbsMsg& msg)
	{
		ProcessMessage(msg.m_Channel, msg.m_Message);
	}

    void WalletNetworkViaBbs::SendRawMessage(const WalletID& peerID, const ByteBuffer& msg)
    {
        // first store message for accidental app close
        auto messageID = m_WalletDB->saveWalletMessage(peerID, msg);
        BbsProcessor::Send(peerID, msg, messageID);
    }

    void WalletNetworkViaBbs::OnChannelAdded(BbsChannel channel)
	{
        SubscribeChannel(channel);
	}

    void WalletNetworkViaBbs::OnChannelDeleted(BbsChannel channel)
    {
        UnsubscribeChannel(channel);
	}

    void WalletNetworkViaBbs::OnMessageSent(uint64_t messageID)
    {
        m_WalletDB->deleteWalletMessage(messageID);
    }

    void WalletNetworkViaBbs::onAddressChanged(ChangeAction action, const vector<WalletAddress>& items)
    {
        switch (action)
        {
        case ChangeAction::Added:
        case ChangeAction::Updated:
            for (const auto& address : items)
            {
                if (!address.isOwn())
                {
                    continue;
                }
                else if (!address.isExpired())
                {
                    AddOwnAddress(address);
                }
                else
                {
                    DeleteOwnAddress(address.m_walletID);
                }
            }
            break;
        case ChangeAction::Removed:
            for (const auto& address : items)
            {
                DeleteOwnAddress(address.m_walletID);
            }
            break;
        case ChangeAction::Reset:
            assert(false && "invalid address change action");
            break;
        }
    }
}
