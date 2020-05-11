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

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#else
#	pragma warning (push, 0)
#   pragma warning( disable : 4996 )
#endif

#include "client.hpp"
#include "queue/working_queue.h"
#include "device_manager.hpp"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (pop)
#endif

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;


    TrezorKeyKeeperProxy::TrezorKeyKeeperProxy(std::shared_ptr<DeviceManager> deviceManager)
        : m_DeviceManager(deviceManager)
        , m_PushEvent(io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { ProcessResponses(); }))
    {
        
    }

    IPrivateKeyKeeper2::Status::Type TrezorKeyKeeperProxy::InvokeSync(Method::get_Kdf& x)
    {
        if (m_OwnerKdf && (x.m_Root 
            || x.m_iChild == Key::Index(-1))) // TODO temporary, remove this condition after integration
        {
            x.m_pPKdf = m_OwnerKdf;
            return Status::Success;
        }
        return PrivateKeyKeeper_AsyncNotify::InvokeSync(x);
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::get_Kdf& x, const Handler::Ptr& h)
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

        m_DeviceManager->call_BeamGetOwnerKey(true, [this, &x, h](const Message& msg, std::string session, size_t queue_size)
        {
            auto pubKdf = std::make_shared<ECC::HKdfPub>();

            ///// Old
            auto ownerKey = child_cast<Message, hw::trezor::messages::beam::BeamOwnerKey>(msg).key();
            KeyString ks;
            ks.SetPassword("LWkeNHJD");
            ks.m_sRes = ownerKey;

            ks.Import(*pubKdf);

            ///// New

            //const ECC::HKdfPub::Packed* packed = reinterpret_cast<const ECC::HKdfPub::Packed*>(child_cast<Message, hw::trezor::messages::beam::BeamOwnerKey>(msg).key().data());

            //pubKdf->Import(*packed);

            PushHandlerToCallerThread([this, &x, h, pubKdf]()
            {
                {
                    
                    x.m_pPKdf = pubKdf;

                    if (x.m_Root)
                    {
                        m_OwnerKdf = pubKdf;
                    }
                }
                PushOut(Status::Success, h);
            });
        });

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

    IPrivateKeyKeeper2::Status::Type TrezorKeyKeeperProxy::InvokeSync(Method::get_NumSlots& x)
    {
        x.m_Count = 8;
        return Status::Success;
    }

    //void TrezorKeyKeeperProxy::InvokeAsync(Method::get_NumSlots& x, const Handler::Ptr& h)
    //{
    //    //json msg =
    //    //{
    //    //    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
    //    //    {"id", 0},
    //    //    {"method", "get_slots"}
    //    //};
    //    //
    //    //_connection.sendAsync(msg, [this, &x, h](const json& msg)
    //    //    {
    //    //        Status::Type s = GetStatus(msg);
    //    //        if (s == Status::Success)
    //    //        {
    //    //            x.m_Count = msg["count"];
    //    //        }
    //    //        PushOut(s, h);
    //    //    });
    //    x.m_Count = 8;
    //    PushOut(Status::Success, h);
    //}

    void TrezorKeyKeeperProxy::InvokeAsync(Method::CreateOutput& x, const Handler::Ptr& h)
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

    void TrezorKeyKeeperProxy::InvokeAsync(Method::SignReceiver& x, const Handler::Ptr& h)
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

    void TrezorKeyKeeperProxy::InvokeAsync(Method::SignSender& x, const Handler::Ptr& h)
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

    void TrezorKeyKeeperProxy::InvokeAsync(Method::SignSplit& x, const Handler::Ptr& h)
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

    void TrezorKeyKeeperProxy::PushHandlerToCallerThread(MessageHandler&& h)
    {
        {
            unique_lock<mutex> lock(m_ResponseMutex);
            m_ResponseHandlers.push(move(h));
        }
        m_PushEvent->post();
    }

    void TrezorKeyKeeperProxy::ProcessResponses()
    {
        while (true)
        {
            MessageHandler h;
            {
                unique_lock<mutex> lock(m_ResponseMutex);
                if (m_ResponseHandlers.empty())
                    return;
                h = std::move(m_ResponseHandlers.front());
                m_ResponseHandlers.pop();
            }
            h();
        }
    }

}