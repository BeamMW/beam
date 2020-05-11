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

#pragma once

#include "wallet/core/private_key_keeper.h"
#include "hw_wallet.h"
#include <queue>
#include <functional>
#include <mutex>

class DeviceManager;

namespace beam::wallet
{
    class TrezorKeyKeeperProxy
        : public PrivateKeyKeeper_AsyncNotify
    {
        using MessageHandler = std::function<void()>;
    public:
        TrezorKeyKeeperProxy(std::shared_ptr<DeviceManager> deviceManager, Key::IPKdf::Ptr ownerKdf = {});
        virtual ~TrezorKeyKeeperProxy() = default;
    private:
        Status::Type InvokeSync(Method::get_Kdf& x) override;
        void InvokeAsync(Method::get_Kdf& x, const Handler::Ptr& h) override;
        //void InvokeAsync(Method::get_NumSlots& x, const Handler::Ptr& h) override;
        Status::Type InvokeSync(Method::get_NumSlots& x) override;
        void InvokeAsync(Method::CreateOutput& x, const Handler::Ptr& h) override;
        void InvokeAsync(Method::SignReceiver& x, const Handler::Ptr& h) override;
        void InvokeAsync(Method::SignSender& x, const Handler::Ptr& h) override;
        void InvokeAsync(Method::SignSplit& x, const Handler::Ptr& h) override;

        //static void GetMutualResult(Method::TxMutual& x, const json& msg)
        //{
        //    x.m_PaymentProofSignature = from_base64<ECC::Signature>(msg["payment_proof_sig"]);
        //    GetCommonResult(x, msg);
        //}
        //
        //static void GetCommonResult(Method::TxCommon& x, const json& msg)
        //{
        //    auto offset = from_base64<ECC::Scalar>(msg["offset"]);
        //    x.m_kOffset.Import(offset);
        //    x.m_pKernel = from_base64<TxKernelStd::Ptr>(msg["kernel"]);
        //}
        //
        //static Status::Type GetStatus(const json& msg)
        //{
        //    return msg["status"];
        //}

        void PushHandlerToCallerThread(MessageHandler&& h);
        void ProcessResponses();

    private:
        std::shared_ptr<DeviceManager> m_DeviceManager;
        Key::IPKdf::Ptr m_OwnerKdf;
        io::AsyncEvent::Ptr m_PushEvent;
        std::queue<IPrivateKeyKeeper2::Handler::Ptr> m_Handlers;
        std::mutex m_ResponseMutex;
        std::queue<MessageHandler> m_ResponseHandlers;
    };
}