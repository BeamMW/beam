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
#include "utility/logger.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (pop)
#endif

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;
    using namespace hw::trezor::messages::beam;

    namespace
    {
        std::string Hex(const std::string& s)
        {
            return "0x" + s;
        }

        void DumpCommonParameters(const IPrivateKeyKeeper2::Method::TxCommon& m)
        {
            using namespace nlohmann;
            auto res = json
            {
                {"tx_common",
                    {
                        {"inputs", json::array()},

                        {"outputs", json::array()},

                        {"offset_sk", Hex(ECC::Scalar(m.m_kOffset).str())},
                        {"kernel_parameters",
                            {
                                {"fee", m.m_pKernel->m_Fee},
                                {"min_height", m.m_pKernel->m_Height.m_Min},
                                {"max_height", m.m_pKernel->m_Height.m_Max},
                                {"commitment",
                                    {
                                        {"x", Hex(m.m_pKernel->m_Commitment.m_X.str())},
                                        {"y", m.m_pKernel->m_Commitment.m_Y}
                                    }
                                },
                                {"signature",
                                    {
                                        {"nonce_pub",
                                            {
                                                {"x", Hex(m.m_pKernel->m_Signature.m_NoncePub.m_X.str())},
                                                {"y", m.m_pKernel->m_Signature.m_NoncePub.m_Y}
                                            }
                                        },
                                        {"k", Hex(m.m_pKernel->m_Signature.m_k.str())}
                                    },

                                }
                            }
                        }
                    },
                }
            };

            auto f = [](json& j, const std::vector<CoinID>& coins)
            {
                for (const auto& c : coins)
                {
                    j.push_back(
                        {
                           {"idx", c.m_Idx},
                           {"type", int(c.m_Type)},
                           {"sub_idx", c.m_SubIdx},
                           {"amount", c.m_Value},
                           {"asset_id", c.m_AssetID}
                        }
                    );
                }
            };

            f(res["tx_common"]["outputs"], m.m_vOutputs);
            f(res["tx_common"]["inputs"], m.m_vInputs);



            std::cout << std::endl << res.dump() << std::endl;
        }

        void DumpSenderParameters(const IPrivateKeyKeeper2::Method::SignSender& m)
        {
            using namespace nlohmann;
            auto res = json
            {
                {"tx_common", 
                    {
                        {"inputs", json::array()},
                    
                        {"outputs", json::array()},
                                 
                        {"offset_sk", Hex(ECC::Scalar(m.m_kOffset).str())},
                        {"kernel_parameters",
                            {
                                {"fee", m.m_pKernel->m_Fee},
                                {"min_height", m.m_pKernel->m_Height.m_Min},
                                {"max_height", m.m_pKernel->m_Height.m_Max},
                                {"commitment",
                                    {
                                        {"x", Hex(m.m_pKernel->m_Commitment.m_X.str())},
                                        {"y", m.m_pKernel->m_Commitment.m_Y}
                                    }
                                },
                                {"signature",
                                    {
                                        {"nonce_pub",
                                            {
                                                {"x", Hex(m.m_pKernel->m_Signature.m_NoncePub.m_X.str())},
                                                {"y", m.m_pKernel->m_Signature.m_NoncePub.m_Y}
                                            }
                                        },
                                        {"k", Hex(m.m_pKernel->m_Signature.m_k.str())}
                                    },
                                    
                                }
                            }
                        }
                    },
                },
                {"tx_mutual_info",
                    {
                        {"peer", Hex(m.m_Peer.str())},
                        {"wallet_identity_key", m.m_MyIDKey},
                        {"payment_proof_signature",
                            {
                                {"nonce_pub",
                                    {
                                        {"x", Hex(m.m_PaymentProofSignature.m_NoncePub.m_X.str())},
                                        {"y", m.m_PaymentProofSignature.m_NoncePub.m_Y}
                                    }
                                },
                                {"k", Hex(m.m_PaymentProofSignature.m_k.str())}
                            },
                        },
                    }
                },

                {"nonce_slot", m.m_Slot},
                {"user_agreement", Hex(m.m_UserAgreement.str())}
            };

            auto f = [](json& j, const std::vector<CoinID>& coins)
            {
                for (const auto& c : coins)
                {
                    j.push_back(
                         {
                            {"idx", c.m_Idx},
                            {"type", int(c.m_Type)},
                            {"sub_idx", c.m_SubIdx},
                            {"amount", c.m_Value},
                            {"asset_id", c.m_AssetID}
                        }
                    );
                }
            };
            
            f(res["tx_common"]["outputs"], m.m_vOutputs);
            f(res["tx_common"]["inputs"], m.m_vInputs);

           
            
            std::cout << std::endl << res.dump() << std::endl;
        }

        std::shared_ptr<DeviceManager> GetTrezor(std::shared_ptr<Client> client, const std::string& deviceName, std::shared_ptr<DeviceManager> existingDevice)
        {
            auto enumerates = client->enumerate();
            if (enumerates.empty())
                return nullptr;
            //auto it = std::find_if(enumerates.begin(), enumerates.end(),
            //    [&deviceName](const auto& d)
            //    {
            //        return deviceName == std::to_string(d);
            //    });
            //
            //if (it == enumerates.end())
            //{
            //    return nullptr;
            //}

            auto& enumerate = enumerates.front();//*it;

            if (enumerate.session != "null" && !existingDevice)
            {
                client->release(enumerate.session);
                enumerate.session = "null";
            }

            auto trezor = existingDevice ? existingDevice : std::make_shared<DeviceManager>();

            if (!existingDevice)
            {
                trezor->callback_Failure([&](const Message& msg, std::string session, size_t queue_size)
                    {
                        auto message = child_cast<Message, Failure>(msg).message();

                        //onError(message);

                        LOG_ERROR() << "TREZOR FAIL REASON: " << message;
                    });

                trezor->callback_Success([&](const Message& msg, std::string session, size_t queue_size)
                    {
                        LOG_INFO() << "TREZOR SUCCESS: " << child_cast<Message, Success>(msg).message();
                    });

                try
                {
                    trezor->init(enumerate);
                }
                catch (std::runtime_error e)
                {
                    LOG_ERROR() << e.what();
                    return nullptr;
                }
            }           

            return trezor;
        }

        BeamCrypto_UintBig& Ecc2BC(const ECC::uintBig& x)
        {
            static_assert(sizeof(x) == sizeof(BeamCrypto_UintBig));
            return (BeamCrypto_UintBig&)x;
        }

        BeamCrypto_CompactPoint& Ecc2BC(const ECC::Point& x)
        {
            static_assert(sizeof(x) == sizeof(BeamCrypto_CompactPoint));
            return (BeamCrypto_CompactPoint&)x;
        }

        void CidCvt(BeamCrypto_CoinID& cid2, const CoinID& cid)
        {
            cid2.m_Idx = cid.m_Idx;
            cid2.m_SubIdx = cid.m_SubIdx;
            cid2.m_Type = cid.m_Type;
            cid2.m_AssetID = cid.m_AssetID;
            cid2.m_Amount = cid.m_Value;
        }

        template<typename T>
        const T& ConvertResultTo(const std::string& result)
        {
            assert(result.size() == sizeof(T));
            return *reinterpret_cast<const T*>(result.data());
        }

        struct Vectors
        {
            std::vector<BeamCrypto_CoinID> m_Inputs;
            std::vector<BeamCrypto_CoinID> m_Outputs;
        };

        void TxImport(BeamCrypto_TxCommon& tx2, const IPrivateKeyKeeper2::Method::TxCommon& m, Vectors& v)
        {
            v.m_Inputs.resize(m.m_vInputs.size());
            v.m_Outputs.resize(m.m_vOutputs.size());

            for (size_t i = 0; i < v.m_Inputs.size(); ++i)
                CidCvt(v.m_Inputs[i], m.m_vInputs[i]);

            for (size_t i = 0; i < v.m_Outputs.size(); ++i)
                CidCvt(v.m_Outputs[i], m.m_vOutputs[i]);

            tx2.m_pIns = &v.m_Inputs;
            tx2.m_pOuts = &v.m_Outputs;
        
            // kernel
            assert(m.m_pKernel);
            tx2.m_Krn.m_Fee = m.m_pKernel->m_Fee;
            tx2.m_Krn.m_hMin = m.m_pKernel->m_Height.m_Min;
            tx2.m_Krn.m_hMax = m.m_pKernel->m_Height.m_Max;

            tx2.m_Krn.m_Commitment = Ecc2BC(m.m_pKernel->m_Commitment);
            tx2.m_Krn.m_Signature.m_NoncePub = Ecc2BC(m.m_pKernel->m_Signature.m_NoncePub);
            tx2.m_Krn.m_Signature.m_k = Ecc2BC(m.m_pKernel->m_Signature.m_k.m_Value);
        
            // offset
            ECC::Scalar kOffs(m.m_kOffset);
            tx2.m_kOffset = Ecc2BC(kOffs.m_Value);
        }

        void TxExport(BeamCrypto_CompactPoint& point, const BeamECCPoint& point2)
        {
            point.m_X = ConvertResultTo<BeamCrypto_UintBig>(point2.x());
            point.m_Y = static_cast<uint8_t>(point2.y());
        }

        void TxExport(BeamCrypto_Signature& signature, const BeamSignature& signature2)
        {
            signature.m_k = ConvertResultTo<BeamCrypto_UintBig>(signature2.sign_k());
            TxExport(signature.m_NoncePub, signature2.nonce_pub());
        }

        void TxExport(BeamCrypto_TxCommon& txCommon, const BeamTxCommon& tx2)
        {
            // kernel
            const auto& kernelParams = tx2.kernel_params();
            txCommon.m_Krn.m_Fee = kernelParams.fee();
            txCommon.m_Krn.m_hMin = kernelParams.min_height();
            txCommon.m_Krn.m_hMax = kernelParams.max_height();
            
            TxExport(txCommon.m_Krn.m_Commitment, kernelParams.commitment());
            TxExport(txCommon.m_Krn.m_Signature, kernelParams.signature());

            // offset
            txCommon.m_kOffset = ConvertResultTo<BeamCrypto_UintBig>(tx2.offset_sk());
        }

        void TxExport(IPrivateKeyKeeper2::Method::TxCommon& m, const BeamCrypto_TxCommon& tx2)
        {
            // kernel
            assert(m.m_pKernel);
            //m.m_pKernel->m_Fee = tx2.m_Krn.m_Fee;
            //m.m_pKernel->m_Height.m_Min = tx2.m_Krn.m_hMin;
            //m.m_pKernel->m_Height.m_Max = tx2.m_Krn.m_hMax;

            Ecc2BC(m.m_pKernel->m_Commitment) = tx2.m_Krn.m_Commitment;
            Ecc2BC(m.m_pKernel->m_Signature.m_NoncePub) = tx2.m_Krn.m_Signature.m_NoncePub;
            Ecc2BC(m.m_pKernel->m_Signature.m_k.m_Value) = tx2.m_Krn.m_Signature.m_k;

            m.m_pKernel->UpdateID();

            // offset
            ECC::Scalar kOffs;
            Ecc2BC(kOffs.m_Value) = tx2.m_kOffset;
            m.m_kOffset = kOffs;
        }
    }

    ///////////
    // Cache
    void TrezorKeyKeeperProxy::Cache::ShrinkMru(uint32_t nCount)
    {
        while (m_mruPKdfs.size() > nCount)
        {
            ChildPKdf& x = m_mruPKdfs.back();
            m_mruPKdfs.pop_back();

            m_setPKdfs.erase(ChildPKdfSet::s_iterator_to(x));
            delete &x;
        }
    }

    void TrezorKeyKeeperProxy::Cache::AddMru(ChildPKdf& x)
    {
        m_mruPKdfs.push_front(x);
    }

    bool TrezorKeyKeeperProxy::Cache::FindPKdf(Key::IPKdf::Ptr& pRes, const Key::Index* pChild)
    {
        pRes.reset(); // for more safety

        std::unique_lock<std::mutex> scope(m_Mutex);

        if (pChild)
        {
            ChildPKdf key;
            key.m_iChild = *pChild;

            ChildPKdfSet::iterator it = m_setPKdfs.find(key);
            if (m_setPKdfs.end() != it)
            {
                ChildPKdf& x = *it;
                m_mruPKdfs.erase(ChildPKdfList::s_iterator_to(x));
                AddMru(x);
                pRes = x.m_pRes;
            }
        }
        else
        {
            pRes = m_pOwner;
        }

        return pRes != nullptr;
    }

    void TrezorKeyKeeperProxy::Cache::AddPKdf(const Key::IPKdf::Ptr& pRes, const Key::Index* pChild)
    {
        std::unique_lock<std::mutex> scope(m_Mutex);

        if (pChild)
        {
            std::unique_ptr<ChildPKdf> pItem = std::make_unique<ChildPKdf>();
            pItem->m_iChild = *pChild;

            ChildPKdfSet::iterator it = m_setPKdfs.find(*pItem);
            if (m_setPKdfs.end() == it)
            {
                pItem->m_pRes = pRes;

                m_setPKdfs.insert(*pItem);
                AddMru(*pItem);
                pItem.release();

                ShrinkMru(1000); // don't let it grow indefinitely
            }
        }
        else
        {
            if (!m_pOwner)
                m_pOwner = pRes;
        }
    }

    //////////
    // TrezorKeyKeeperProxy
    TrezorKeyKeeperProxy::TrezorKeyKeeperProxy(std::shared_ptr<Client> client, const std::string& deviceName, HWWallet::IHandler::Ptr uiHandler)
        : m_Client(client)
        , m_DeviceName(deviceName)
        , m_UIHandler(uiHandler)
    {
        EnsureEvtOut();
    }

    IPrivateKeyKeeper2::Status::Type TrezorKeyKeeperProxy::InvokeSync(Method::get_Kdf& m)
    {
        if (m_Cache.FindPKdf(m.m_pPKdf, m.m_Root ? nullptr : &m.m_iChild))
            return Status::Success;

        return PrivateKeyKeeper_WithMarshaller::InvokeSync(m);
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::get_Kdf& m, const Handler::Ptr& h)
    {
        if (m.m_Root)
        {
            m.m_iChild = 0; // with another value we will get "Firmware error"
        }
        PushHandler(h);
        //ShowUI();
        GetDevice()->call_BeamGetPKdf(m.m_Root, m.m_iChild, false, 
            [this, &m, h](const Message& msg, std::string session, size_t queue_size)
        {
           // HideUI();
            PopHandler();
            const BeamPKdf& beamKdf = child_cast<Message, BeamPKdf>(msg);
            typedef struct
            {
                BeamCrypto_UintBig m_Secret;
                BeamCrypto_CompactPoint m_CoFactorG;
                BeamCrypto_CompactPoint m_CoFactorJ;

            } BeamCrypto_KdfPub;
            BeamCrypto_KdfPub pkdf;
            pkdf.m_Secret = ConvertResultTo<BeamCrypto_UintBig>(beamKdf.key());
            
            TxExport(pkdf.m_CoFactorG, beamKdf.cofactor_g());
            TxExport(pkdf.m_CoFactorJ, beamKdf.cofactor_j());

            ECC::HKdfPub::Packed packed;
            Ecc2BC(packed.m_Secret) = pkdf.m_Secret;
            Ecc2BC(packed.m_PkG) = pkdf.m_CoFactorG;
            Ecc2BC(packed.m_PkJ) = pkdf.m_CoFactorJ;

            auto pubKdf = std::make_shared<ECC::HKdfPub>();
            if (pubKdf->Import(packed))
            {
                m_Cache.AddPKdf(pubKdf, m.m_Root ? nullptr : &m.m_iChild);

                m.m_pPKdf = std::move(pubKdf);
                PushOut(Status::Success, h);
            }
            else
            {
                PushOut(Status::Unspecified, h);
            }
        });
    }

    IPrivateKeyKeeper2::Status::Type TrezorKeyKeeperProxy::InvokeSync(Method::get_NumSlots& m)
    {
        {
            std::unique_lock<std::mutex> scope(m_Cache.m_Mutex);
            if (m_Cache.m_Slots)
            {
                m.m_Count = m_Cache.m_Slots;
                return Status::Success;
            }
        }

        return PrivateKeyKeeper_WithMarshaller::InvokeSync(m);
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::get_NumSlots& m, const Handler::Ptr& h)
    {
        PushHandler(h);
        GetDevice()->call_BeamGetNumSlots(false, 
            [this, &m, h](const Message& msg, std::string session, size_t queue_size)
        {
            PopHandler();
            m.m_Count = child_cast<Message, BeamNumSlots>(msg).num_slots();

            if (m.m_Count)
            {
                {
                    std::unique_lock<std::mutex> scope(m_Cache.m_Mutex);
                    m_Cache.m_Slots = m.m_Count;
                }

                PushOut(Status::Success, h);
            }
            else
            {
                PushOut(Status::Unspecified, h);
            }
        });
    }

    struct TrezorKeyKeeperProxy::CreateOutputCtx
        :public TaskFin
    {
        typedef std::unique_ptr<CreateOutputCtx> Ptr;

        TrezorKeyKeeperProxy& m_This;

        Method::CreateOutput& m_Method;

        Key::IPKdf::Ptr m_pOwner;
        Key::IPKdf::Ptr m_pChild;
        Output::Ptr m_pRes;

        CreateOutputCtx(TrezorKeyKeeperProxy& x, Method::CreateOutput& m) 
            : m_This(x)
            , m_Method(m){}
        virtual ~CreateOutputCtx() {}

        void Proceed1(Ptr&);
        void Proceed2(Ptr&);
        void Proceed3(Ptr&);

        static void Push(Ptr& p, Status::Type s)
        {
            p->m_Status = s;
            TrezorKeyKeeperProxy& x = p->m_This;
            Task::Ptr pTask = std::move(p);
            x.PushOut1(pTask);
        }

        struct HandlerBase
            :public Handler
        {
            CreateOutputCtx::Ptr m_pCtx;

            virtual void OnDone(Status::Type s) override
            {
                if (Status::Success == s)
                    OnDoneOk();
                else
                    Push(m_pCtx, s);
            }

            virtual void OnDoneOk() = 0;
        };

        // TaskFin
        void Execute(Task::Ptr&) override;
    };

    void TrezorKeyKeeperProxy::CreateOutputCtx::Proceed1(Ptr& p)
    {
        if (m_This.m_Cache.FindPKdf(m_pOwner, nullptr))
            Proceed2(p);
        else
        {
            struct MyHandler :public HandlerBase
            {
                Method::get_Kdf m_Method;

                virtual ~MyHandler() {}
                virtual void OnDoneOk() override
                {
                    m_pCtx->m_pOwner = std::move(m_Method.m_pPKdf);
                    m_pCtx->Proceed2(m_pCtx);
                }
            };

            Handler::Ptr pHandler = std::make_shared<MyHandler>();
            MyHandler& h = Cast::Up<MyHandler>(*pHandler);

            h.m_Method.m_iChild = 0;
            h.m_Method.m_Root = true;

            m_This.InvokeAsync(h.m_Method, pHandler);
        }
    }

    void TrezorKeyKeeperProxy::CreateOutputCtx::Proceed2(Ptr& p)
    {
        Method::get_Kdf m;
        m.From(m_Method.m_Cid);

        if (m.m_Root)
            m_pChild = m_pOwner;
        else
        {
            if (!m_This.m_Cache.FindPKdf(m_pChild, &m.m_iChild))
            {
                struct MyHandler :public HandlerBase
                {
                    Method::get_Kdf m_Method;

                    virtual ~MyHandler() {}
                    virtual void OnDoneOk() override
                    {
                        m_pCtx->m_pChild = std::move(m_Method.m_pPKdf);
                        m_pCtx->Proceed3(m_pCtx);
                    }
                };

                Handler::Ptr pHandler = std::make_shared<MyHandler>();
                MyHandler& h = Cast::Up<MyHandler>(*pHandler);

                h.m_Method = m;
                m_This.InvokeAsync(h.m_Method, pHandler);

                return;
            }
        }

        Proceed3(p);
    }

    void TrezorKeyKeeperProxy::CreateOutputCtx::Proceed3(Ptr& p)
    {
        m_pRes = std::make_unique<Output>();
        
        ECC::Point::Native comm;
        CoinID::Worker(m_Method.m_Cid).Recover(comm, *m_pChild);
        m_pRes->m_Commitment = comm;
        
        // rangeproof
        ECC::Scalar::Native skDummy;
        ECC::HKdf kdfDummy;
        
        m_pRes->Create(m_Method.m_hScheme, skDummy, kdfDummy, m_Method.m_Cid, *m_pOwner, Output::OpCode::Mpc_1, &m_Method.m_User);
        assert(m_pRes->m_pConfidential);

        BeamCrypto_CoinID cid;
        BeamCrypto_CompactPoint pt0, pt1;

        CidCvt(cid, m_Method.m_Cid);
        pt0 = Ecc2BC(m_pRes->m_pConfidential->m_Part2.m_T1);
        pt1 = Ecc2BC(m_pRes->m_pConfidential->m_Part2.m_T2);

        // hack, to capture the std::unique_ptr.
        // Similar to deprecated auto_ptr

        struct AutoMovePtr
        {
            mutable Ptr m_p;
            AutoMovePtr(Ptr&& p) :m_p(std::move(p)) {}
            AutoMovePtr(const AutoMovePtr& x)
            {
                m_p.swap(x.m_p);
            }
        };

        static_assert(sizeof(BeamCrypto_UintBig) == sizeof(ECC::Scalar));
        static_assert(_countof(m_Method.m_User.m_pExtra) == 2);
        const BeamCrypto_UintBig* pExtra = reinterpret_cast<const BeamCrypto_UintBig*>(m_Method.m_User.m_pExtra);

        AutoMovePtr amp(std::move(p));
        m_This.PushHandler(amp.m_p->m_pHandler);
        m_This.GetDevice()->call_BeamGenerateRangeproof(&cid, &pt0, &pt1, pExtra[0], pExtra[1],
            [this, amp](const Message& msg, std::string session, size_t queue_size)
        {
            m_This.PopHandler();
            const BeamRangeproofData& brpd = child_cast<Message, BeamRangeproofData>(msg);

            if (!brpd.is_successful())
            {
                Push(amp.m_p, Status::Unspecified);
                return;
            }

            ECC::Scalar::Native tauX;
            m_pRes->m_pConfidential->m_Part3.m_TauX = ConvertResultTo<ECC::Scalar>(brpd.data_taux());

            BeamCrypto_CompactPoint bccp;
            TxExport(bccp, brpd.pt0());
            Ecc2BC(m_pRes->m_pConfidential->m_Part2.m_T1) = bccp;

            TxExport(bccp, brpd.pt1());
            Ecc2BC(m_pRes->m_pConfidential->m_Part2.m_T2) = bccp;

            Push(amp.m_p, Status::Success); // final stage is deferred
        });
    }

    void TrezorKeyKeeperProxy::CreateOutputCtx::Execute(Task::Ptr& p)
    {
        if (Status::Success == m_Status)
        {
            ECC::Scalar::Native skDummy;
            ECC::HKdf kdfDummy;
            m_pRes->Create(m_Method.m_hScheme, skDummy, kdfDummy, m_Method.m_Cid, *m_pOwner, Output::OpCode::Mpc_2, &m_Method.m_User); // Phase 3

            m_Method.m_pResult.swap(m_pRes);
        }

        TaskFin::Execute(p);
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::CreateOutput& m, const Handler::Ptr& h)
    {
        if (m.m_hScheme < Rules::get().pForks[1].m_Height)
            return PushOut(Status::NotImplemented, h);
        
        CreateOutputCtx::Ptr pCtx = std::make_unique<CreateOutputCtx>(*this, m);
        pCtx->m_pHandler = h;

        pCtx->Proceed1(pCtx);
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::SignReceiver& m, const Handler::Ptr& h)
    {
        BeamCrypto_TxCommon txCommon;
        Vectors v;
        TxImport(txCommon, m, v);

        BeamCrypto_TxMutualInfo txMutualInfo;
        txMutualInfo.m_MyIDKey = m.m_MyIDKey;
        txMutualInfo.m_Peer = Ecc2BC(m.m_Peer);

        PushHandler(h);
        GetDevice()->call_BeamSignTransactionReceive(txCommon, txMutualInfo, 
            [this, &m, h, txCommon](const Message& msg, std::string session, size_t queue_size) mutable
        {
            PopHandler();
            const auto& txReceive = child_cast<Message, BeamSignTransactionReceive>(msg);
            TxExport(txCommon, txReceive.tx_common());
            BeamCrypto_Signature proofSignature;
            TxExport(proofSignature, txReceive.tx_mutual_info().payment_proof_signature());

            TxExport(m, txCommon);
            Ecc2BC(m.m_PaymentProofSignature.m_NoncePub) = proofSignature.m_NoncePub;
            Ecc2BC(m.m_PaymentProofSignature.m_k.m_Value) = proofSignature.m_k;
            PushOut(Status::Success, h);
        });
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::SignSender& m, const Handler::Ptr& h)
    {
        BeamCrypto_TxCommon txCommon;
        Vectors v;
        TxImport(txCommon, m, v);

        BeamCrypto_TxMutualInfo txMutualInfo;
        txMutualInfo.m_MyIDKey = m.m_MyIDKey;
        txMutualInfo.m_Peer = Ecc2BC(m.m_Peer);
        txMutualInfo.m_PaymentProofSignature.m_NoncePub = Ecc2BC(m.m_PaymentProofSignature.m_NoncePub);
        txMutualInfo.m_PaymentProofSignature.m_k = Ecc2BC(m.m_PaymentProofSignature.m_k.m_Value);

        BeamCrypto_TxSenderParams txSenderParams;
        txSenderParams.m_iSlot = m.m_Slot;
        txSenderParams.m_UserAgreement = Ecc2BC(m.m_UserAgreement);

        PushHandler(h);
        ShowUI();
        //DumpSenderParameters(m);
        GetDevice()->call_BeamSignTransactionSend(txCommon, txMutualInfo, txSenderParams,
            [this, &m, h, txCommon](const Message& msg, std::string session, size_t queue_size) mutable
        {
            HideUI();
            PopHandler();
            TxExport(txCommon, child_cast<Message, BeamSignTransactionSend>(msg).tx_common());
            BeamCrypto_UintBig userAgreement = ConvertResultTo<BeamCrypto_UintBig>(child_cast<Message, BeamSignTransactionSend>(msg).user_agreement());

            TxExport(m, txCommon);
            Ecc2BC(m.m_UserAgreement) = userAgreement;
            PushOut(Status::Success, h);
        });
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::SignSplit& m, const Handler::Ptr& h)
    {
        BeamCrypto_TxCommon txCommon;
        Vectors v;
        TxImport(txCommon, m, v);

        PushHandler(h);
        ShowUI();
        //DumpCommonParameters(m);
        GetDevice()->call_BeamSignTransactionSplit(txCommon,
            [this, &m, h, txCommon](const Message& msg, std::string session, size_t queue_size) mutable
        {
            HideUI();
            PopHandler();
            TxExport(txCommon, child_cast<Message, BeamSignTransactionSplit>(msg).tx_common());
            TxExport(m, txCommon);
            PushOut(Status::Success, h);
        });
    }

    void TrezorKeyKeeperProxy::PushOut1(Task::Ptr& p)
    {
        PrivateKeyKeeper_WithMarshaller::PushOut(p);
    }

    void TrezorKeyKeeperProxy::PushHandler(const Handler::Ptr& handler)
    {
        m_Handlers.push(handler);
    }

    void TrezorKeyKeeperProxy::PopHandler()
    {
        assert(!m_Handlers.empty());
        if (!m_Handlers.empty())
        {
            m_Handlers.pop();
        }
    }

    void TrezorKeyKeeperProxy::ShowUI()
    {
        if (m_UIHandler)
        {
            m_UIHandler->ShowKeyKeeperMessage();
        }
    }

    void TrezorKeyKeeperProxy::HideUI()
    {
        if (m_UIHandler)
        {
            m_UIHandler->HideKeyKeeperMessage();
        }
    }

    std::shared_ptr<DeviceManager> TrezorKeyKeeperProxy::GetDevice()
    {
        {
            auto deviceManager = GetTrezor(m_Client, m_DeviceName, m_DeviceManager);
            if (!deviceManager)
            {
                if (m_UIHandler)
                {
                    m_UIHandler->ShowKeyKeeperError("HW wallet device is not connected");
                }
                throw std::runtime_error("no HW device");
            }
            if (!m_DeviceManager)
            {
                deviceManager->callback_Failure([&](const Message& msg, std::string session, size_t queue_size)
                    {
                        auto message = child_cast<Message, Failure>(msg).message();
                        HideUI();
                        if (m_Handlers.empty())
                        {
                            return;
                        }
                        PushOut(Status::Unspecified, m_Handlers.front());
                        m_Handlers.pop();
                    });

            }
            m_DeviceManager = deviceManager;
        }
        return m_DeviceManager;
    }
}