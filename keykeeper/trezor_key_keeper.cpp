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

#include "trezor_key_keeper.h"
#include "core/block_rw.h"
#include "utility/logger.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    HardwareKeyKeeperProxy::HardwareKeyKeeperProxy()
       // : m_OwnerKdf(ownerKdf)
    {}

    IPrivateKeyKeeper2::Status::Type HardwareKeyKeeperProxy::InvokeSync(Method::get_Kdf& x)
    {
        //if (x.m_Root)
        //{
        //    assert(_ownerKdf);
        //    x.m_pPKdf = _ownerKdf;
        //    return Status::Success;
        //}
        return PrivateKeyKeeper_AsyncNotify::InvokeSync(x);
    }

    void HardwareKeyKeeperProxy::InvokeAsync(Method::get_Kdf& x, const Handler::Ptr& h)
    {
        //json msg =
        //{
        //    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
        //    {"id", 0},
        //    {"method", "get_kdf"},
        //    {"params",
        //        {
        //            {"root", x.m_Root},
        //            {"child_key_num", x.m_iChild}
        //        }
        //    }
        //};
        //
        //
        //_connection.sendAsync(msg, [this, &x, h](const json& msg)
        //    {
        //        Status::Type s = GetStatus(msg);
        //        if (s == Status::Success)
        //        {
        //            ByteBuffer buf = from_base64<ByteBuffer>(msg["pub_kdf"]);
        //            ECC::HKdfPub::Packed* packed = reinterpret_cast<ECC::HKdfPub::Packed*>(&buf[0]);
        //
        //            auto pubKdf = std::make_shared<ECC::HKdfPub>();
        //            pubKdf->Import(*packed);
        //            x.m_pPKdf = pubKdf;
        //        }
        //        PushOut(s, h);
        //    });
    }

    void HardwareKeyKeeperProxy::InvokeAsync(Method::get_NumSlots& x, const Handler::Ptr& h)
    {
        //json msg =
        //{
        //    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
        //    {"id", 0},
        //    {"method", "get_slots"}
        //};
        //
        //_connection.sendAsync(msg, [this, &x, h](const json& msg)
        //    {
        //        Status::Type s = GetStatus(msg);
        //        if (s == Status::Success)
        //        {
        //            x.m_Count = msg["count"];
        //        }
        //        PushOut(s, h);
        //    });
    }

    void HardwareKeyKeeperProxy::InvokeAsync(Method::CreateOutput& x, const Handler::Ptr& h)
    {
        //json msg =
        //{
        //    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
        //    {"id", 0},
        //    {"method", "create_output"},
        //    {"params",
        //        {
        //            {"scheme", to_base64(x.m_hScheme)},
        //            {"id", to_base64(x.m_Cid)}
        //        }
        //    }
        //};
        //
        //_connection.sendAsync(msg, [this, &x, h](const json& msg)
        //    {
        //        Status::Type s = GetStatus(msg);
        //        if (s == Status::Success)
        //        {
        //            x.m_pResult = from_base64<Output::Ptr>(msg["result"]);
        //        }
        //        PushOut(s, h);
        //    });
    }

    void HardwareKeyKeeperProxy::InvokeAsync(Method::SignReceiver& x, const Handler::Ptr& h)
    {
        //json msg =
        //{
        //    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
        //    {"id", 0},
        //    {"method", "sign_receiver"},
        //    {"params",
        //        {
        //            {"inputs",    to_base64(x.m_vInputs)},
        //            {"outputs",   to_base64(x.m_vOutputs)},
        //            {"kernel",    to_base64(x.m_pKernel)},
        //            {"non_conv",  x.m_NonConventional},
        //            {"peer_id",   to_base64(x.m_Peer)},
        //            {"my_id_key", to_base64(x.m_MyIDKey)}
        //        }
        //    }
        //};
        //
        //_connection.sendAsync(msg, [this, &x, h](const json& msg)
        //    {
        //        Status::Type s = GetStatus(msg);
        //        if (s == Status::Success)
        //        {
        //            GetMutualResult(x, msg);
        //        }
        //        PushOut(s, h);
        //    });
    }

    void HardwareKeyKeeperProxy::InvokeAsync(Method::SignSender& x, const Handler::Ptr& h)
    {
        //json msg =
        //{
        //    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
        //    {"id", 0},
        //    {"method", "sign_sender"},
        //    {"params",
        //        {
        //            {"inputs",    to_base64(x.m_vInputs)},
        //            {"outputs",   to_base64(x.m_vOutputs)},
        //            {"kernel",    to_base64(x.m_pKernel)},
        //            {"non_conv",  x.m_NonConventional},
        //            {"peer_id",   to_base64(x.m_Peer)},
        //            {"my_id_key", to_base64(x.m_MyIDKey)},
        //            {"slot",      x.m_Slot},
        //            {"agreement", to_base64(x.m_UserAgreement)},
        //            {"my_id",     to_base64(x.m_MyID)},
        //            {"payment_proof_sig", to_base64(x.m_PaymentProofSignature)}
        //        }
        //    }
        //};
        //
        //_connection.sendAsync(msg, [this, &x, h](const json& msg)
        //    {
        //        Status::Type s = GetStatus(msg);
        //
        //        if (s == Status::Success)
        //        {
        //            if (x.m_UserAgreement == Zero)
        //            {
        //                x.m_UserAgreement = from_base64<ECC::Hash::Value>(msg["agreement"]);
        //                x.m_pKernel->m_Commitment = from_base64<ECC::Point>(msg["commitment"]);
        //                x.m_pKernel->m_Signature.m_NoncePub = from_base64<ECC::Point>(msg["pub_nonce"]);
        //            }
        //            else
        //            {
        //                GetCommonResult(x, msg);
        //            }
        //        }
        //        PushOut(s, h);
        //    });
    }

    void HardwareKeyKeeperProxy::InvokeAsync(Method::SignSplit& x, const Handler::Ptr& h)
    {
        //json msg =
        //{
        //    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
        //    {"id", 0},
        //    {"method", "sign_split"},
        //    {"params",
        //        {
        //            {"inputs",   to_base64(x.m_vInputs)},
        //            {"outputs",  to_base64(x.m_vOutputs)},
        //            {"kernel",   to_base64(x.m_pKernel)},
        //            {"non_conv", x.m_NonConventional}
        //        }
        //    }
        //};
        //
        //_connection.sendAsync(msg, [this, &x, h](const json& msg)
        //    {
        //        Status::Type s = GetStatus(msg);
        //        if (s == Status::Success)
        //        {
        //            GetCommonResult(x, msg);
        //        }
        //        PushOut(s, h);
        //    });
    }


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


//    ////////////////////////////////////
//
//    TrezorKeyKeeper::TrezorKeyKeeper()
//        : m_hwWallet([this](const std::string& msg)
//            {
//                for (auto handler : m_handlers)
//                    handler->onShowKeyKeeperError(msg);
//            })
//        , m_latestSlot(0)
//    {
//
//    }
//
//    TrezorKeyKeeper::~TrezorKeyKeeper()
//    {
//
//    }
//
//    Key::IKdf::Ptr TrezorKeyKeeper::get_SbbsKdf() const
//    {
//        // !TODO: temporary solution to init SBBS KDF with commitment
//        // also, we could store SBBS Kdf in the WalletDB
//
//        if (!m_sbbsKdf)
//        {
//            if (m_hwWallet.isConnected())
//            {
//                ECC::HKdf::Create(m_sbbsKdf, m_hwWallet.generateKeySync({ 0, 0, Key::Type::Regular }, true).m_X);
//            }
//        }
//           
//        return m_sbbsKdf;
//    }
//
//    void TrezorKeyKeeper::GeneratePublicKeys(const std::vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback)
//    {
//        assert(!"not implemented.");
//    }
//
//    void TrezorKeyKeeper::GenerateOutputs(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback)
//    {
//        using namespace std;
//
//        auto thisHolder = shared_from_this();
//        shared_ptr<Outputs> result = make_shared<Outputs>();
//        shared_ptr<exception> storedException;
//        shared_ptr<future<void>> futureHolder = make_shared<future<void>>();
//        *futureHolder = do_thread_async(
//            [thisHolder, this, schemeHeight, ids, result, storedException]()
//            {
//                try
//                {
//                    *result = GenerateOutputsSync(schemeHeight, ids);
//                }
//                catch (const exception& ex)
//                {
//                    *storedException = ex;
//                }
//            },
//            [futureHolder, resultCallback = move(resultCallback), exceptionCallback = move(exceptionCallback), result, storedException]() mutable
//            {
//                if (storedException)
//                {
//                    exceptionCallback(*storedException);
//                }
//                else
//                {
//                    resultCallback(move(*result));
//                }
//                futureHolder.reset();
//            });
//    }
//
//    size_t TrezorKeyKeeper::AllocateNonceSlotSync()
//    {
//        m_latestSlot++;
//        m_hwWallet.generateNonceSync((uint8_t)m_latestSlot);
//        return m_latestSlot;
//    }
//
//    IPrivateKeyKeeper::PublicKeys TrezorKeyKeeper::GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey)
//    {
//        PublicKeys result;
//        result.reserve(ids.size());
//
//        for (const auto& idv : ids)
//        {
//            ECC::Point& publicKey = result.emplace_back();
//            publicKey = m_hwWallet.generateKeySync(idv, createCoinKey);
//        }
//
//        return result;
//    }
//
//    ECC::Point TrezorKeyKeeper::GeneratePublicKeySync(const Key::IDV& id)
//    {
//        return m_hwWallet.generateKeySync(id, false);
//    }
//
//    IPrivateKeyKeeper::Outputs TrezorKeyKeeper::GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids)
//    {
//        Outputs outputs;
//        outputs.reserve(ids.size());
//
//        for (const auto& kidv : ids)
//        {
//            auto& output = outputs.emplace_back(std::make_unique<Output>());
//            output->m_Commitment = m_hwWallet.generateKeySync(kidv, true);
//
//            output->m_pConfidential.reset(new ECC::RangeProof::Confidential);
//            *output->m_pConfidential = m_hwWallet.generateRangeProofSync(kidv, false);
//        }
//
//        return outputs;
//    }
//
//    ECC::Point TrezorKeyKeeper::GenerateNonceSync(size_t slot)
//    {
//        assert(m_latestSlot >= slot);
//        return m_hwWallet.getNoncePublicSync((uint8_t)slot);
//    }
//
//    ECC::Scalar TrezorKeyKeeper::SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, Asset::ID assetId, const ECC::Scalar::Native& offset, size_t nonceSlot, const KernelParameters& kernelParamerters, const ECC::Point::Native& publicNonce)
//    {
//        // TODO:ASSETS implement
//        assert(assetId == 0);
//        HWWallet::TxData txData;
//        txData.fee = kernelParamerters.fee;
//        txData.height = kernelParamerters.height;
//        txData.kernelCommitment = kernelParamerters.commitment;
//        txData.kernelNonce = publicNonce;
//        txData.nonceSlot = (uint32_t)nonceSlot;
//        txData.offset = offset;
//
//        for (auto handler : m_handlers)
//            handler->onShowKeyKeeperMessage();
//
//        auto res = m_hwWallet.signTransactionSync(inputs, outputs, txData);
//
//        for (auto handler : m_handlers)
//            handler->onHideKeyKeeperMessage();
//
//        return res;
//    }
//
//    void TrezorKeyKeeper::subscribe(Handler::Ptr handler)
//    {
//        assert(std::find(m_handlers.begin(), m_handlers.end(), handler) == m_handlers.end());
//
//        m_handlers.push_back(handler);
//    }
}