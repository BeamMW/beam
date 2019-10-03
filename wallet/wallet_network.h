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
                uint64_t m_OwnID;
                bool operator < (const Wid& x) const { return m_OwnID < x.m_OwnID; }
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
            PeerID m_Pk; // self public addr
            Timestamp m_ExpirationTime;
        };
    public:
        BaseMessageEndpoint(IWalletMessageConsumer&, const IWalletDB::Ptr&, IPrivateKeyKeeper::Ptr);
        virtual ~BaseMessageEndpoint();
        void AddOwnAddress(const WalletAddress& address);
        void DeleteOwnAddress(uint64_t ownID);
    protected:
        void ProcessMessage(BbsChannel channel, const ByteBuffer& msg);
        void Subscribe();
        void Unsubscribe();
        virtual void OnChannelAdded(BbsChannel channel) {};
        virtual void OnChannelDeleted(BbsChannel channel) {};
        virtual void OnIncomingMessage() {};
    private:
        void DeleteAddr(const Addr&);
        bool IsSingleChannelUser(const Addr::Channel&);

        // IWalletMessageEndpoint
        void Send(const WalletID& peerID, const SetTxParameter& msg) override;
        void SendAndSign(const ByteBuffer& msg, const BbsChannel& channel, const WalletID& wid, uint8_t version) override;
        void OnAddressTimer();
        
    private:
        typedef bi::multiset<Addr::Wid> WidSet;
        WidSet m_Addresses;

        typedef  bi::multiset<Addr::Channel> ChannelSet;
        ChannelSet m_Channels;

        IWalletMessageConsumer& m_Wallet;
        IWalletDB::Ptr m_WalletDB;
        io::Timer::Ptr m_AddressExpirationTimer;

        IPrivateKeyKeeper::Ptr m_keyKeeper;
    };

    class WalletNetworkViaBbs
        : public BaseMessageEndpoint
        , private IWalletDbObserver
    {
        std::shared_ptr<proto::FlyClient::INetwork> m_NodeEndpoint;
        IWalletDB::Ptr m_WalletDB;

        struct MyRequestBbsMsg
            :public proto::FlyClient::RequestBbsMsg
            ,public boost::intrusive::list_base_hook<>
        {
            typedef boost::intrusive_ptr<MyRequestBbsMsg> Ptr;
            virtual ~MyRequestBbsMsg() {}
        };

        typedef boost::intrusive::list<MyRequestBbsMsg> BbsMsgList;
        BbsMsgList m_PendingBbsMsgs;

        void DeleteReq(MyRequestBbsMsg& r);

        struct BbsSentEvt
            :public proto::FlyClient::Request::IHandler
            ,public proto::FlyClient::IBbsReceiver
        {
            virtual void OnComplete(proto::FlyClient::Request&) override;
            virtual void OnMsg(proto::BbsMsg&&) override;
            IMPLEMENT_GET_PARENT_OBJ(WalletNetworkViaBbs, m_BbsSentEvt)
        } m_BbsSentEvt;

        void OnMsg(const proto::BbsMsg&);

        std::unordered_map<BbsChannel, Timestamp> m_BbsTimestamps;
        io::Timer::Ptr m_pTimerBbsTmSave;
        void OnTimerBbsTmSave();
        void SaveBbsTimestamps();

        struct Miner
        {
            // message mining
            std::vector<std::thread> m_vThreads;
            std::mutex m_Mutex;
            std::condition_variable m_NewTask;

            volatile bool m_Shutdown;
            io::AsyncEvent::Ptr m_pEvt;

            struct Task
            {
                proto::BbsMsg m_Msg;
                ECC::Hash::Processor m_hpPartial;
                volatile bool m_Done;

                typedef std::shared_ptr<Task> Ptr;
            };

            typedef std::deque<Task::Ptr> TaskQueue;

            TaskQueue m_Pending;
            TaskQueue m_Done;

            Miner() :m_Shutdown(false) {}
            ~Miner() { Stop(); }

            void Stop();
            void Thread(uint32_t);

        } m_Miner;

        void OnMined();
        void OnMined(proto::BbsMsg&&);

    public:

        WalletNetworkViaBbs(IWalletMessageConsumer&, std::shared_ptr<proto::FlyClient::INetwork>, const IWalletDB::Ptr&, IPrivateKeyKeeper::Ptr);
        virtual ~WalletNetworkViaBbs();

        bool m_MineOutgoing = true; // can be turned-off for testing

    private:
        void OnChannelAdded(BbsChannel channel) override;
        void OnChannelDeleted(BbsChannel channel) override;
        // IWalletMessageEndpoint
        void SendEncryptedMessage(const WalletID& peerID, const ByteBuffer& msg) override;
        void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;
    };

    class ColdWalletMessageEndpoint
        : public BaseMessageEndpoint
    {
    public:
        ColdWalletMessageEndpoint(IWalletMessageConsumer& wallet, IWalletDB::Ptr walletDB, IPrivateKeyKeeper::Ptr keyKeeper);
        ~ColdWalletMessageEndpoint();
    private:
        void SendEncryptedMessage(const WalletID& peerID, const ByteBuffer& msg) override;
    private:
        IWalletDB::Ptr m_WalletDB;
    };
}
