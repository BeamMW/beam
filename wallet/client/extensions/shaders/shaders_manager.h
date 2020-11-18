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

#include "bvm/ManagerStd.h"
#include "wallet/core/wallet_db.h"
#include "utility/logger.h"

class ShadersManager
    :  private beam::bvm2::ManagerStd
{
public:
    struct IDone {
        virtual void onShaderDone() = 0;
    };

    using Kind = ManagerStd::Kind;

    ShadersManager(beam::wallet::IWalletDB::Ptr walletDB, beam::proto::FlyClient::INetwork::Ptr nodeNetwork, IDone& doneHandler);
    ~ShadersManager();

    bool IsDone() const
    {
        return _done;
    }

    bool IsError() const
    {
        return !_error.empty();
    }

    const std::string& GetError() const
    {
        return _error;
    }

    std::vector<beam::bvm2::ContractInvokeData> GetInvokeData() const
    {
        return m_vInvokeData;
    }

    std::string GetResult() const
    {
        auto result = m_Out.str();
        LOG_INFO () << "SHader result: " << result;
        return IsError() ? "" : m_Out.str();
    }

    void Compile(const std::vector<uint8_t>& shader, Kind kind); // throws
    void Start(const std::string& args, unsigned method); // throws

protected:
    void OnDone(const std::exception* pExc) override;

private:
    bool _done  = true;
    bool _async = false;

    std::string _error;
    std::string _args;

    beam::wallet::IWalletDB::Ptr _wdb;
    IDone& _doneHandler;
};
