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

#include "bvm/invoke_data.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/wallet.h"

namespace beam::wallet
{
    class IShadersManager
    {
    public:
        typedef std::function <void (boost::optional<TxID> txid, boost::optional<std::string> output, boost::optional<std::string> error)> DoneAllHandler;
        typedef std::function <void (boost::optional<ByteBuffer> data, boost::optional<std::string> output, boost::optional<std::string> error)> DoneCallHandler;
        typedef std::function <void (boost::optional<TxID> txid, boost::optional<std::string> error)> DoneTxHandler;

        typedef std::shared_ptr<IShadersManager> Ptr;
        typedef std::weak_ptr<IShadersManager> WeakPtr;

        static Ptr CreateInstance(
                beam::wallet::Wallet::Ptr wallet,
                beam::wallet::IWalletDB::Ptr wdb,
                beam::proto::FlyClient::INetwork::Ptr nodeNetwork,
                std::string appid,
                std::string appname);

        virtual ~IShadersManager() = default;

        // CallShaderAndStartTx - call shader & automatically create transaction if necessary
        // CallShader - only make call and return tx data, doesn't create any transactions
        // ProcessTxData - process data returned by CallShader
        virtual void CallShaderAndStartTx(const std::vector<uint8_t>& shader, const std::string& args, unsigned method, uint32_t priority, DoneAllHandler doneHandler) = 0;
        virtual void CallShader(const std::vector<uint8_t>& shader, const std::string& args, unsigned method, uint32_t priority, DoneCallHandler) = 0;
        virtual void ProcessTxData(const ByteBuffer& data, DoneTxHandler doneHandler) = 0;
        [[nodiscard]] virtual  bool IsDone() const = 0;
    };
}
