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

namespace beam::wallet
{
    class HardwareKeyKeeperProxy
        : public PrivateKeyKeeper_AsyncNotify
    {
    public:
        HardwareKeyKeeperProxy();
        virtual ~HardwareKeyKeeperProxy() = default;
    private:
        Status::Type InvokeSync(Method::get_Kdf& x) override;
        void InvokeAsync(Method::get_Kdf& x, const Handler::Ptr& h) override;
        void InvokeAsync(Method::get_NumSlots& x, const Handler::Ptr& h) override;
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

    private:
        //Key::IPKdf::Ptr m_OwnerKdf;
    };


    //class TrezorKeyKeeper : public IPrivateKeyKeeper
    //    , public std::enable_shared_from_this<TrezorKeyKeeper>
    //{
    //public:
    //    TrezorKeyKeeper();
    //    virtual ~TrezorKeyKeeper();
    //
    //    struct DeviceNotConnected : std::runtime_error 
    //    {
    //        DeviceNotConnected() : std::runtime_error("") {}
    //    };
    //
    //    Key::IKdf::Ptr get_SbbsKdf() const override;
    //    void subscribe(Handler::Ptr handler) override;
    //
    //private:
    //    void GeneratePublicKeys(const std::vector<CoinID>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback) override;
    //    void GenerateOutputs(Height schemeHeight, const std::vector<CoinID>& ids, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback) override;
    //
    //    size_t AllocateNonceSlotSync() override;
    //    PublicKeys GeneratePublicKeysSync(const std::vector<CoinID>& ids, bool createCoinKey) override;
    //
    //    ECC::Point GeneratePublicKeySync(const ECC::uintBig& id) override;
    //    ECC::Point GenerateCoinKeySync(const CoinID& id) override;
    //    Outputs GenerateOutputsSync(Height schemeHeigh, const std::vector<CoinID>& ids) override;
    //    ECC::Point GenerateNonceSync(size_t slot) override;
    //
    //private:
    //    beam::HWWallet m_hwWallet;
    //    mutable Key::IKdf::Ptr m_sbbsKdf;
    //
    //    size_t m_latestSlot;
    //
    //    std::vector<IPrivateKeyKeeper::Handler::Ptr> m_handlers;
    //};
}