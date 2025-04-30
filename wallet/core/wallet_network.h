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

#include "utility/logger.h"
#include "core/proto.h"
#include "utility/io/timer.h"
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>
#include "wallet_request_bbs_msg.h"
#include "wallet.h"

namespace beam::wallet
{
    namespace bi = boost::intrusive;

    class BaseMessageEndpoint
        : public IWalletMessageEndpoint
    {
        struct Addr
        {
            struct Wid :public boost::intrusive::set_base_hook<> {
                WalletID m_Value;
                IHandler* m_pHandler;

                bool operator < (const Wid& x) const
                {
                    if (m_pHandler < x.m_pHandler)
                        return true;
                    if (m_pHandler > x.m_pHandler)
                        return false;
                    return m_Value < x.m_Value;
                }

                IMPLEMENT_GET_PARENT_OBJ(Addr, m_Wid)
            } m_Wid;

            struct Channel :public boost::intrusive::set_base_hook<> {
                BbsChannel m_Value;
                bool operator < (const Channel& x) const { return m_Value < x.m_Value; }
                IMPLEMENT_GET_PARENT_OBJ(Addr, m_Channel)
            } m_Channel;

            bool IsExpired() const
            {
                return getTimestamp() > m_ExpirationTime;
            }

            ECC::Scalar::Native m_sk; // private addr
            Timestamp m_ExpirationTime = 0;
            uint32_t m_Refs = 0;
            static const uint32_t s_InternalRef = 0x10000000;

        };
    public:
        BaseMessageEndpoint(IWalletMessageConsumer&, const IWalletDB::Ptr&);
        virtual ~BaseMessageEndpoint();
        void AddOwnAddress(const WalletAddress& address);
        void DeleteOwnAddress(const WalletID&);
    protected:
        void ProcessMessage(const proto::BbsMsg& msg);
        void Subscribe();
        void Unsubscribe();
        virtual void OnChannelAdded(BbsChannel channel) {};
        virtual void OnChannelDeleted(BbsChannel channel) {};
        virtual void OnIncomingMessage() {};
    private:
        Addr* FindAddr(const WalletID&, IHandler*);
        void DeleteAddr(const Addr&);
        void ReleaseAddr(Addr&, bool bInternalRef);
        bool IsSingleChannelUser(const Addr::Channel&);
        Addr* CreateAddr(const WalletID&, IHandler* );

        // IWalletMessageEndpoint
        void Send(const WalletID& peerID, const SetTxParameter& msg) override;
        void Send(const WalletID& peerID, const Blob&) override;
        void Listen(const WalletID&, const ECC::Scalar::Native&, IHandler*) override;
        void Unlisten(const WalletID&, IHandler*) override;
        void OnAddressTimer();
        
    private:
        typedef bi::multiset<Addr::Wid> WidSet;
        WidSet m_Addresses;

        typedef  bi::multiset<Addr::Channel> ChannelSet;
        ChannelSet m_Channels;

        IWalletMessageConsumer& m_Wallet;
        IWalletDB::Ptr m_WalletDB;
        Key::IKdf::Ptr m_pKdfSbbs;
        io::Timer::Ptr m_AddressExpirationTimer;
    };
    struct ITimestampHolder
    {
        using Ptr = std::shared_ptr<ITimestampHolder>;
        virtual ~ITimestampHolder() = default;
        virtual Timestamp GetTimestamp(BbsChannel channel) = 0;
        virtual void UpdateTimestamp(const proto::BbsMsg& msg) = 0;
    };

    class TimestampHolder : public ITimestampHolder
    {
    public:
        TimestampHolder(IWalletDB::Ptr walletDB, const char* timestampsKey);
        virtual ~TimestampHolder();

        Timestamp GetTimestamp(BbsChannel channel);
        void UpdateTimestamp(const proto::BbsMsg& msg);
    private:
        void SaveBbsTimestamps();
        void OnTimerBbsTmSave();
    private:
        IWalletDB::Ptr m_WalletDB;
        std::unordered_map<BbsChannel, Timestamp> m_BbsTimestamps;
        io::Timer::Ptr m_pTimerBbsTmSave;
        std::string_view m_TimestampsKey;
    };

    class BbsProcessor
    {
    public:
        BbsProcessor(proto::FlyClient::INetwork::Ptr nodeEndpoint, ITimestampHolder::Ptr);
        virtual ~BbsProcessor();
        void Send(const WalletID& peerID, ByteBuffer&& msg, uint64_t messageID);

        void SubscribeChannel(BbsChannel channel);
        void UnsubscribeChannel(BbsChannel channel);

        virtual void OnMessageSent(uint64_t messageID) {}
        virtual void OnMsg(const proto::BbsMsg&) {};
    private:
        proto::FlyClient::IBbsReceiver* get_BbsReceiver();
        void OnTimerBbsTmSave();
        void SaveBbsTimestamps();
        void OnMsgImpl(const proto::BbsMsg&);
        void DeleteReq(WalletRequestBbsMsg& r);
    private:
        proto::FlyClient::INetwork::Ptr m_NodeEndpoint;
        BbsMsgList m_PendingBbsMsgs;

        struct BbsSentEvt
            :public proto::FlyClient::Request::IHandler
            , public proto::FlyClient::IBbsReceiver
        {
            virtual void OnComplete(proto::FlyClient::Request&) override;
            virtual void OnMsg(proto::BbsMsg&&) override;

            IMPLEMENT_GET_PARENT_OBJ(BbsProcessor, m_BbsSentEvt)
        } m_BbsSentEvt;

        ITimestampHolder::Ptr m_TimestampHolder;
    };

    class WalletNetworkViaBbs
        : public BaseMessageEndpoint
        , private IWalletDbObserver
        , public BbsProcessor
    {
        IWalletDB::Ptr m_WalletDB;
    public:
        WalletNetworkViaBbs(IWalletMessageConsumer&, proto::FlyClient::INetwork::Ptr, const IWalletDB::Ptr&);
        virtual ~WalletNetworkViaBbs();
    private:
        // BaseMessageEndpoint
        void OnChannelAdded(BbsChannel channel) override;
        void OnChannelDeleted(BbsChannel channel) override;
        void OnMessageSent(uint64_t messageID) override;
        void OnMsg(const proto::BbsMsg&) override;
        // IWalletMessageEndpoint
        void SendRawMessage(const WalletID& peerID, ByteBuffer&&) override;
        void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;
    };
}
