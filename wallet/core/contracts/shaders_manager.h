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
#include "i_shaders_manager.h"
#include "bvm/ManagerStd.h"

namespace beam::wallet {

    struct ManagerStdInWallet
        :public bvm2::ManagerStd
    {
        ManagerStdInWallet(WalletDB::Ptr, Wallet::Ptr);
        virtual ~ManagerStdInWallet();

        void set_Privilege(uint32_t);

    protected:

        WalletDB::Ptr m_pWalletDB;
        Wallet::Ptr m_pWallet;
        uint32_t m_Privilege;

        struct SlotName;
        struct Channel;

        void TestCommAllowed() const;

        bool SlotLoad(ECC::Hash::Value&, uint32_t iSlot) override;
        void SlotSave(const ECC::Hash::Value&, uint32_t iSlot) override;
        void SlotErase(uint32_t iSlot) override;
        void Comm_CreateListener(Comm::Channel::Ptr&, const ECC::Hash::Value&) override;
        void Comm_Send(const ECC::Point&, const Blob&) override;
    };






    class ShadersManager
        : public IShadersManager
        , private ManagerStdInWallet
    {
    public:
        ShadersManager(beam::wallet::Wallet::Ptr wallet,
                       beam::wallet::IWalletDB::Ptr walletDB,
                       beam::proto::FlyClient::INetwork::Ptr nodeNetwork,
                       std::string appid,
                       std::string appname);

        bool IsDone() const override
        {
            return _done;
        }

        void CallShaderAndStartTx(const std::vector<uint8_t>& shader, const std::string& args, unsigned method, uint32_t priority, uint32_t unique, DoneAllHandler doneHandler) override;
        void CallShader(const std::vector<uint8_t>& shader, const std::string& args, unsigned method, uint32_t priority, uint32_t unique, DoneCallHandler) override;
        void ProcessTxData(const ByteBuffer& data, DoneTxHandler doneHandler) override;

    protected:
        void OnDone(const std::exception *pExc) override;

    private:
        void nextRequest();

        // this one throws
        void compileAppShader(const std::vector<uint8_t> &shader);

        bool _done = true;
        bool _logResult = true;
        std::string _currentAppId;
        std::string _currentAppName;

        beam::wallet::Wallet::Ptr _wallet;

        struct Request {
            uint32_t unique = 0;
            uint32_t priority = 0;
            uint32_t method = 0;
            std::vector<uint8_t> shader;
            std::string args;

            DoneAllHandler doneAll;
            DoneCallHandler doneCall;

            bool operator< (const Request& rhs) const
            {
                return priority < rhs.priority;
            }
        };

        void pushRequest(Request req);

        struct RequestsQueue: std::priority_queue<Request> {
            [[nodiscard]] auto begin() const { return c.begin(); }
            [[nodiscard]] auto end() const { return c.end(); }
        } _queue;
    };
}
