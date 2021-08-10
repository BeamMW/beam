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
    class ShadersManager
        : public IShadersManager
        , private beam::bvm2::ManagerStd
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

        void CallShaderAndStartTx(const std::vector<uint8_t>& shader, const std::string& args, unsigned method, DoneAllHandler doneHandler) override;
        void CallShader(const std::vector<uint8_t>& shader, const std::string& args, unsigned method, DoneCallHandler) override;
        void ProcessTxData(const ByteBuffer& data, DoneTxHandler doneHandler) override;

    protected:
        void OnDone(const std::exception *pExc) override;

    private:
        // this one throws
        void compileAppShader(const std::vector<uint8_t> &shader);

        bool _done = true;
        bool _async = false;

        std::string _currentAppId;
        std::string _currentAppName;

        beam::wallet::IWalletDB::Ptr _wdb;
        beam::wallet::Wallet::Ptr _wallet;

        DoneAllHandler  _doneAll;
        DoneCallHandler _doneCall;
    };
}
