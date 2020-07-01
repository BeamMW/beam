// Copyright 2018-2020 The Beam Team
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
#include "keykeeper_proxy.h"
#include "wallet/api/api.h"

namespace beam::wallet {
    WasmKeyKeeperProxy::WasmKeyKeeperProxy(Key::IPKdf::Ptr ownerKdf, IKeykeeperConnection& connection)
        : _ownerKdf(std::move(ownerKdf))
        , _connection(connection)
    {
    }

    WasmKeyKeeperProxy::Status::Type WasmKeyKeeperProxy::InvokeSync(Method::get_Kdf& x)
    {
        if (x.m_Root)
        {
            assert(_ownerKdf);
            x.m_pPKdf = _ownerKdf;
            return Status::Success;
        }
        return PrivateKeyKeeper_WithMarshaller::InvokeSync(x);
    }

    void WasmKeyKeeperProxy::InvokeAsync(Method::get_Kdf& x, const Handler::Ptr& h)
    {
        json msg =
        {
            {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
            {"id", 0},
            {"method", "get_kdf"},
            {"params",
                {
                    {"root", x.m_Root},
                    {"child_key_num", x.m_iChild}
                }
            }
        };

        _connection.invokeKeykeeperAsync(msg, [this, &x, h](const json& msg)
            {
                Status::Type s = GetStatus(msg);
                if (s == Status::Success)
                {
                    ByteBuffer buf = from_base64<ByteBuffer>(msg["pub_kdf"]);
                    ECC::HKdfPub::Packed* packed = reinterpret_cast<ECC::HKdfPub::Packed*>(&buf[0]);

                    auto pubKdf = std::make_shared<ECC::HKdfPub>();
                    pubKdf->Import(*packed);
                    x.m_pPKdf = pubKdf;
                }
                PushOut(s, h);
            });
    }

    void WasmKeyKeeperProxy::InvokeAsync(Method::get_NumSlots& x, const Handler::Ptr& h)
    {
        json msg =
        {
            {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
            {"id", 0},
            {"method", "get_slots"}
        };

        _connection.invokeKeykeeperAsync(msg, [this, &x, h](const json& msg)
        {
            Status::Type s = GetStatus(msg);
            if (s == Status::Success)
            {
                x.m_Count = msg["count"];
            }
            PushOut(s, h);
        });
    }

    void WasmKeyKeeperProxy::InvokeAsync(Method::CreateOutput& x, const Handler::Ptr& h)
    {
        json msg =
        {
            {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
            {"id", 0},
            {"method", "create_output"},
            {"params",
                {
                    {"scheme", to_base64(x.m_hScheme)},
                    {"id", to_base64(x.m_Cid)}
                }
            }
        };

        _connection.invokeKeykeeperAsync(msg, [this, &x, h](const json& msg)
            {
                Status::Type s = GetStatus(msg);
                if (s == Status::Success)
                {
                    x.m_pResult = from_base64<Output::Ptr>(msg["result"]);
                }
                PushOut(s, h);
            });
    }

    void WasmKeyKeeperProxy::InvokeAsync(Method::SignReceiver& x, const Handler::Ptr& h)
    {
        json msg =
        {
            {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
            {"id", 0},
            {"method", "sign_receiver"},
            {"params",
                {
                    {"inputs",    to_base64(x.m_vInputs)},
                    {"outputs",   to_base64(x.m_vOutputs)},
                    {"kernel",    to_base64(x.m_pKernel)},
                    {"non_conv",  x.m_NonConventional},
                    {"peer_id",   to_base64(x.m_Peer)},
                    {"my_id_key", to_base64(x.m_MyIDKey)}
                }
            }
        };

        _connection.invokeKeykeeperAsync(msg, [this, &x, h](const json& msg)
            {
                Status::Type s = GetStatus(msg);
                if (s == Status::Success)
                {
                    GetMutualResult(x, msg);
                }
                PushOut(s, h);
            });
    }

    void WasmKeyKeeperProxy::InvokeAsync(Method::SignSender& x, const Handler::Ptr& h)
    {
        json msg =
        {
            {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
            {"id", 0},
            {"method", "sign_sender"},
            {"params",
                {
                    {"inputs",    to_base64(x.m_vInputs)},
                    {"outputs",   to_base64(x.m_vOutputs)},
                    {"kernel",    to_base64(x.m_pKernel)},
                    {"non_conv",  x.m_NonConventional},
                    {"peer_id",   to_base64(x.m_Peer)},
                    {"my_id_key", to_base64(x.m_MyIDKey)},
                    {"slot",      x.m_Slot},
                    {"agreement", to_base64(x.m_UserAgreement)},
                    {"my_id",     to_base64(x.m_MyID)},
                    {"payment_proof_sig", to_base64(x.m_PaymentProofSignature)}
                }
            }
        };

        _connection.invokeKeykeeperAsync(msg, [this, &x, h](const json& msg)
            {
                Status::Type s = GetStatus(msg);

                if (s == Status::Success)
                {
                    if (x.m_UserAgreement == Zero)
                    {
                        x.m_UserAgreement = from_base64<ECC::Hash::Value>(msg["agreement"]);
                        x.m_pKernel->m_Commitment = from_base64<ECC::Point>(msg["commitment"]);
                        x.m_pKernel->m_Signature.m_NoncePub = from_base64<ECC::Point>(msg["pub_nonce"]);
                    }
                    else
                    {
                        GetCommonResult(x, msg);
                    }
                }
                PushOut(s, h);
            });
    }

    void WasmKeyKeeperProxy::InvokeAsync(Method::SignSplit& x, const Handler::Ptr& h)
    {
        json msg =
        {
            {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
            {"id", 0},
            {"method", "sign_split"},
            {"params",
                {
                    {"inputs",   to_base64(x.m_vInputs)},
                    {"outputs",  to_base64(x.m_vOutputs)},
                    {"kernel",   to_base64(x.m_pKernel)},
                    {"non_conv", x.m_NonConventional}
                }
            }
        };

        _connection.invokeKeykeeperAsync(msg, [this, &x, h](const json& msg)
            {
                Status::Type s = GetStatus(msg);
                if (s == Status::Success)
                {
                    GetCommonResult(x, msg);
                }
                PushOut(s, h);
            });
    }

    void WasmKeyKeeperProxy::GetMutualResult(Method::TxMutual& x, const json& msg)
    {
        x.m_PaymentProofSignature = from_base64<ECC::Signature>(msg["payment_proof_sig"]);
        GetCommonResult(x, msg);
    }

    void WasmKeyKeeperProxy::GetCommonResult(Method::TxCommon& x, const json& msg)
    {
        auto offset = from_base64<ECC::Scalar>(msg["offset"]);
        x.m_kOffset.Import(offset);
        x.m_pKernel = from_base64<TxKernelStd::Ptr>(msg["kernel"]);
    }

    WasmKeyKeeperProxy::Status::Type WasmKeyKeeperProxy::GetStatus(const json& msg)
    {
        return msg["status"];
    }
}
