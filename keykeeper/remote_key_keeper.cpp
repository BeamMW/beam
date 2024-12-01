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

#include "remote_key_keeper.h"
#include "utility/byteorder.h"
#include "utility/executor.h"
#include "utility/containers.h"

namespace beam {
namespace hw {

extern "C" {
#   include "../hw_crypto/keykeeper.h"
}


#define THE_MACRO_Field(type, name) type m_##name;
#define THE_MACRO_Field_h2n(type, name) Proto::h2n(m_##name);

	namespace Proto
	{
		template <typename T> void h2n_u(T& x) {
            x = ByteOrder::to_le(x);
		}

        void h2n(char& x) { }
        void h2n(uint8_t& x) { }
        void h2n(uint16_t& x) { h2n_u(x); }
		void h2n(uint32_t& x) { h2n_u(x); }
		void h2n(uint64_t& x) { h2n_u(x); }

        void h2n(ShieldedInput_Blob& x) {}

		void h2n(ShieldedInput_Fmt& x) {
			h2n(x.m_Fee);
			h2n(x.m_Amount);
			h2n(x.m_AssetID);
			h2n(x.m_nViewerIdx);
		}

        void h2n(ShieldedInput_SpendParams& x) {
            h2n(x.m_hMin);
            h2n(x.m_hMax);
            h2n(x.m_WindowEnd);
            h2n(x.m_Sigma_M);
            h2n(x.m_Sigma_n);
        }

		void h2n(CoinID& cid) {
			h2n(cid.m_Amount);
			h2n(cid.m_AssetID);
			h2n(cid.m_Idx);
			h2n(cid.m_SubIdx);
			h2n(cid.m_Type);
		}

		void h2n(TxCommonIn& tx) {
			h2n(tx.m_Krn.m_Fee);
			h2n(tx.m_Krn.m_hMin);
			h2n(tx.m_Krn.m_hMax);
		}

		void h2n(TxMutualIn& mut) {
			h2n(mut.m_AddrID);
		}

        void h2n(KdfPub& x) {}
        void h2n(UintBig& x) {}
        void h2n(CompactPoint& x) {}
        void h2n(TxCommonOut& x) {}
        void h2n(TxSig& x) {}
        void h2n(TxKernelCommitments& x) {}
        void h2n(ShieldedVoucher& x) {}
        void h2n(ShieldedTxoUser& x) {}
        void h2n(Signature& x) {}
        void h2n(RangeProof_Packed& x) {}

#pragma pack (push, 1)

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Warray-bounds" // can popup during the following macro expansion. Ignore it.
#endif

#define THE_MACRO(id, name) \
		struct name { \
			struct Out { \
                typedef name Pair; \
				uint8_t m_OpCode; \
				Out() { \
					ZeroObject(*this); \
					m_OpCode = id; \
				} \
				BeamCrypto_ProtoRequest_##name(THE_MACRO_Field) \
				void h2n() { BeamCrypto_ProtoRequest_##name(THE_MACRO_Field_h2n) } \
			}; \
			struct In { \
				uint8_t m_StatusCode; \
				BeamCrypto_ProtoResponse_##name(THE_MACRO_Field) \
				void n2h() { BeamCrypto_ProtoResponse_##name(THE_MACRO_Field_h2n) } \
			}; \
		};

		BeamCrypto_ProtoMethods(THE_MACRO)
#undef THE_MACRO
#undef THE_MACRO_Field
#pragma pack (pop)


#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#   pragma GCC diagnostic pop
#endif

	} // namespace Proto



} // namespace hw
} // namespace beam

namespace beam::wallet
{

    //////////////
    // Cache

    bool RemoteKeyKeeper::Cache::get_Owner(Key::IPKdf::Ptr& pRes)
    {
        //std::unique_lock<std::mutex> scope(m_Mutex);

        pRes = m_pOwner;
        return pRes != nullptr;
    }

    void RemoteKeyKeeper::Cache::set_Owner(const Key::IPKdf::Ptr& pRes)
    {
        //std::unique_lock<std::mutex> scope(m_Mutex);

        if (!m_pOwner)
            m_pOwner = pRes;
    }

    uint32_t RemoteKeyKeeper::Cache::get_NumSlots()
    {
        //std::unique_lock<std::mutex> scope(m_Mutex);
        return m_Slots;
    }


    RemoteKeyKeeper::Status::Type RemoteKeyKeeper::DeduceStatus(uint8_t* pBuf, uint32_t nResponse, uint32_t nResponseActual)
    {
        if (!nResponseActual)
            return Status::Unspecified;

        uint8_t retVal = *pBuf;
        if (c_KeyKeeper_Status_Ok != retVal)
        {
            switch (retVal)
            {
            case c_KeyKeeper_Status_UserAbort:
                return Status::UserAbort;

            case c_KeyKeeper_Status_NotImpl:
                return Status::NotImplemented;

            default:
                return Status::Unspecified;
            }
        }

        if (nResponse != nResponseActual)
            return Status::Unspecified;

        return Status::Success;
    }

    //////////////
    // Impl
    struct RemoteKeyKeeper::Impl
    {
        static void CidCvt(hw::CoinID&, const CoinID&);

		static void Import(hw::TxKernelUser&, const Method::TxCommon&);
		static void Import(hw::TxKernelCommitments&, const Method::TxCommon&);
        static void Import(hw::TxCommonOut&, const Method::TxCommon&);
		static void Import(hw::TxMutualIn&, const Method::TxMutual&);
		static void Import(hw::ShieldedInput_Blob&, hw::ShieldedInput_Fmt&, const ShieldedTxo::ID&, Amount fee);
		static void Import(hw::ShieldedTxoUser&, const ShieldedTxo::User&);

		static void Export(Method::TxCommon&, const hw::TxCommonOut&);

	    static Amount CalcTxBalance(Asset::ID* pAid, const Method::TxCommon&);
	    static void CalcTxBalance(Amount&, Asset::ID* pAid, Amount, Asset::ID);


        struct RemoteCall
            :public IPrivateKeyKeeper2::Handler
            ,public std::enable_shared_from_this<IPrivateKeyKeeper2::Handler>
        {
            RemoteKeyKeeper& m_This;
            Handler::Ptr m_pFinal;

            struct ReqNode
                :public boost::intrusive::list_base_hook<>
            {
                typedef std::unique_ptr<ReqNode> Ptr;

                uint32_t m_Request;
                uint32_t m_Response;

                void* operator new(size_t n, uint32_t nExtra) {
                    return new uint8_t[n + nExtra];
                }

                void operator delete(void* p) {
                    delete[] reinterpret_cast<uint8_t*>(p);
                }

                void operator delete(void* p, uint32_t) {
                    delete[] reinterpret_cast<uint8_t*>(p);
                }
            };

            intrusive::list_autoclear<ReqNode> m_lstPending;
            intrusive::list_autoclear<ReqNode> m_lstDone;
            uint32_t m_Phase = 0;

            RemoteCall(RemoteKeyKeeper& kk, Handler::Ptr&& h)
                :m_This(kk)
                ,m_pFinal(h)
            {
                m_This.m_InProgress++;
            }

            virtual ~RemoteCall()
            {
                assert(m_This.m_InProgress);
                --m_This.m_InProgress;
                m_This.CheckPending();
            }

            void* AllocReq(uint32_t nRequest, uint32_t nResponse)
            {
                ReqNode::Ptr pGuard(new (std::max(nRequest, nResponse)) ReqNode);
                pGuard->m_Request = nRequest;
                pGuard->m_Response = nResponse;

                auto pRet = pGuard.get() + 1;
                m_lstPending.push_back(*pGuard.release());
                return pRet;
            }

            void SendReq()
            {
                assert(!m_lstPending.empty());
                auto& r = m_lstPending.back();

                m_Phase++;
                m_This.SendRequestAsync(&r + 1, r.m_Request, r.m_Response, shared_from_this());
            }

            void SendDummyReq()
            {
                AllocReq(0, 0);
                m_Phase++;
            }

            template <typename TRequest>
            void* AllocReq_T(const TRequest& msg, uint32_t nExtraRequest = 0, uint32_t nExtraResponse = 0)
            {
                auto pMsg = (TRequest*) AllocReq(sizeof(TRequest) + nExtraRequest, sizeof(typename TRequest::Pair::In) + nExtraResponse);
                memcpy(pMsg, &msg, sizeof(msg));
                pMsg->h2n();

                return pMsg + 1;
            }


            template <typename TRequest>
            void SendReq_T(const TRequest& msg, uint32_t nExtraResponse = 0)
            {
                AllocReq_T(msg, 0, nExtraResponse);
                SendReq();
            }


            template <typename TPair>
            typename TPair::In* ReadReq_T()
            {
                while (true)
                {
                    if (m_lstDone.empty())
                        return nullptr;

                    auto& r0 = m_lstDone.front();
                    if (r0.m_Request)
                        break;

                    m_lstDone.Delete(r0);
                }

                auto& r = m_lstDone.front();
                assert(r.m_Response >= sizeof(typename TPair::In));

                r.m_Request = 0;
                m_Phase++;

                auto pRes = reinterpret_cast<typename TPair::In*>(&r + 1);
                pRes->n2h();
                return pRes;
            }

            void Fin(Status::Type n = Status::Success)
            {
                m_This.PushOut(n, std::move(m_pFinal));
            }

            virtual void OnDone(Status::Type n) override
            {
                if (Status::Success == n)
                {
                    auto& r = m_lstPending.front();
                    m_lstPending.pop_front();
                    m_lstDone.push_back(r);

                    if (!r.m_Request && !r.m_Response)
                    {
                        // dummy
                        m_lstDone.Delete(r);
                        m_Phase++;
                    }

                    Update();
                }
                else
                    Fin(n);
            }

            virtual void Update() = 0;
        };


#define THE_MACRO(method) \
        struct RemoteCall_##method;
        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO


        struct RemoteCall_WithOwnerKey;
        struct RemoteCall_WithCoins;
    };

    hw::UintBig& Ecc2BC(const ECC::uintBig& x)
    {
        static_assert(sizeof(x) == sizeof(hw::UintBig));
        return (hw::UintBig&) x;
    }

    hw::CompactPoint& Ecc2BC(const ECC::Point& x)
    {
        static_assert(sizeof(x) == sizeof(hw::CompactPoint));
        return (hw::CompactPoint&) x;
    }


    void RemoteKeyKeeper::Impl::CidCvt(hw::CoinID& cid2, const CoinID& cid)
    {
        cid2.m_Idx = cid.m_Idx;
        cid2.m_SubIdx = cid.m_SubIdx;
        cid2.m_Type = cid.m_Type;
        cid2.m_AssetID = cid.m_AssetID;
        cid2.m_Amount = cid.m_Value;
    }

    void RemoteKeyKeeper::Impl::Import(hw::TxKernelUser& krn, const Method::TxCommon& m)
    {
	    assert(m.m_pKernel);
	    const auto& src = *m.m_pKernel;

	    krn.m_Fee = src.m_Fee;
	    krn.m_hMin = src.m_Height.m_Min;
	    krn.m_hMax = src.m_Height.m_Max;
    }

    void RemoteKeyKeeper::Impl::Import(hw::TxKernelCommitments& krn, const Method::TxCommon& m)
    {
	    assert(m.m_pKernel);
	    const auto& src = *m.m_pKernel;

	    krn.m_Commitment = Ecc2BC(src.m_Commitment);
	    krn.m_NoncePub = Ecc2BC(src.m_Signature.m_NoncePub);
    }

    void RemoteKeyKeeper::Impl::Import(hw::TxCommonOut& txOut, const Method::TxCommon& m)
    {
	    Import(txOut.m_Comms, m);

	    ECC::Scalar kOffs(m.m_kOffset);
	    txOut.m_TxSig.m_kOffset = Ecc2BC(kOffs.m_Value);
        txOut.m_TxSig.m_kSig = Ecc2BC(m.m_pKernel->m_Signature.m_k.m_Value);
    }

    void RemoteKeyKeeper::Impl::Import(hw::TxMutualIn& txIn, const Method::TxMutual& m)
    {
	    txIn.m_AddrID = m.m_iEndpoint;
	    txIn.m_Peer = Ecc2BC(m.m_Peer);
    }

    void RemoteKeyKeeper::Impl::Export(Method::TxCommon& m, const hw::TxCommonOut& txOut)
    {
	    // kernel
	    assert(m.m_pKernel);
	    auto& krn = *m.m_pKernel;

	    Ecc2BC(krn.m_Commitment) = txOut.m_Comms.m_Commitment;
	    Ecc2BC(krn.m_Signature.m_NoncePub) = txOut.m_Comms.m_NoncePub;
	    Ecc2BC(krn.m_Signature.m_k.m_Value) = txOut.m_TxSig.m_kSig;

	    krn.UpdateID();

	    // offset
	    ECC::Scalar kOffs;
	    Ecc2BC(kOffs.m_Value) = txOut.m_TxSig.m_kOffset;
	    m.m_kOffset = kOffs;
    }

    void RemoteKeyKeeper::Impl::Import(hw::ShieldedTxoUser& dst, const ShieldedTxo::User& src)
    {
	    dst.m_Sender = Ecc2BC(src.m_Sender);

	    static_assert(_countof(dst.m_pMessage) == _countof(src.m_pMessage));
	    for (uint32_t i = 0; i < _countof(src.m_pMessage); i++)
		    dst.m_pMessage[i] = Ecc2BC(src.m_pMessage[i]);
    }

    void RemoteKeyKeeper::Impl::Import(hw::ShieldedInput_Blob& blob, hw::ShieldedInput_Fmt& fmt, const ShieldedTxo::ID& src, Amount fee)
    {
	    Import(blob.m_User, src.m_User);

	    fmt.m_Amount = src.m_Value;
	    fmt.m_AssetID = src.m_AssetID;
        fmt.m_Fee = fee;

	    blob.m_IsCreatedByViewer = !!src.m_Key.m_IsCreatedByViewer;
	    fmt.m_nViewerIdx = src.m_Key.m_nIdx;
	    blob.m_kSerG = Ecc2BC(src.m_Key.m_kSerG.m_Value);
    }


    Amount RemoteKeyKeeper::Impl::CalcTxBalance(Asset::ID* pAid, const Method::TxCommon& m)
    {
        Amount ret = 0;

        for (const auto& x : m.m_vOutputs)
            CalcTxBalance(ret, pAid, 0 - x.m_Value, x.m_AssetID);

        for (const auto& x : m.m_vInputs)
            CalcTxBalance(ret, pAid, x.m_Value, x.m_AssetID);

        for (const auto& x : m.m_vInputsShielded)
        {
            CalcTxBalance(ret, pAid, x.m_Value, x.m_AssetID);
            CalcTxBalance(ret, pAid, 0 - x.m_Fee, 0);
        }

        return ret;
    }

    void RemoteKeyKeeper::Impl::CalcTxBalance(Amount& res, Asset::ID* pAid, Amount val, Asset::ID aid)
    {
        if ((!pAid) == (!aid))
        {
            res += val;
            if (pAid)
                *pAid = aid;
        }
    }

    //////////////
    // RemoteKeyKeeper
    RemoteKeyKeeper::RemoteKeyKeeper()
    {
        EnsureEvtOut();
    }

    IPrivateKeyKeeper2::Status::Type RemoteKeyKeeper::InvokeSync(Method::get_Kdf& m)
    {
        switch (m.m_Type)
        {
        case KdfType::Root:
            if (m_Cache.get_Owner(m.m_pPKdf))
                return Status::Success;

            // no break;

        case KdfType::Sbbs:
            break;

        default:
            return Status::Unspecified;
        }

        return PrivateKeyKeeper_WithMarshaller::InvokeSync(m);
    }

    IPrivateKeyKeeper2::Status::Type RemoteKeyKeeper::InvokeSync(Method::get_NumSlots& m)
    {
        m.m_Count = m_Cache.get_NumSlots();
        if (m.m_Count)
            return Status::Success;

        return PrivateKeyKeeper_WithMarshaller::InvokeSync(m);
    }




    struct RemoteKeyKeeper::Impl::RemoteCall_get_Kdf
        :public RemoteCall
    {
        RemoteCall_get_Kdf(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::get_Kdf& m)
            :RemoteCall(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::get_Kdf& m_M;

        void Update() override
        {
            if (!m_Phase)
            {
                if ((KdfType::Root == m_M.m_Type) && m_This.m_Cache.get_Owner(m_M.m_pPKdf))
                {
                    Fin();
                    return;
                }

                hw::Proto::GetPKdf::Out msg;
                msg.m_Kind = (KdfType::Root == m_M.m_Type) ? 0 : 1;
                SendReq_T(msg);
            }

            if (1 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::GetPKdf>();
                if (!pMsg)
                    return;

                ECC::HKdfPub::Packed p;
                Ecc2BC(p.m_Secret) = pMsg->m_Value.m_Secret;
                Ecc2BC(p.m_PkG) = pMsg->m_Value.m_CoFactorG;
                Ecc2BC(p.m_PkJ) = pMsg->m_Value.m_CoFactorJ;

                auto pPKdf = std::make_shared<ECC::HKdfPub>();
                if (pPKdf->Import(p))
                {
                    if (KdfType::Root == m_M.m_Type)
                        m_This.m_Cache.set_Owner(pPKdf);

                    m_M.m_pPKdf = std::move(pPKdf);
                    Fin();
                }
                else
                    Fin(Status::Unspecified);
            }

        }

    };


    struct RemoteKeyKeeper::Impl::RemoteCall_get_NumSlots
        :public RemoteCall
    {
        RemoteCall_get_NumSlots(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::get_NumSlots& m)
            :RemoteCall(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::get_NumSlots& m_M;

        void Update() override
        {
            if (!m_Phase)
            {
                m_M.m_Count = m_This.m_Cache.get_NumSlots();
                if (m_M.m_Count)
                {
                    Fin();
                    return;
                }

                hw::Proto::GetNumSlots::Out msg;
                SendReq_T(msg);
            }

            if (1 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::GetNumSlots>();
                if (!pMsg)
                    return;

                {
                    //std::unique_lock<std::mutex> scope(m_This.m_Cache.m_Mutex);
                    m_This.m_Cache.m_Slots = pMsg->m_Value;
                }

                m_M.m_Count = pMsg->m_Value;
                Fin();
            }
        }
    };

    struct RemoteKeyKeeper::Impl::RemoteCall_WithOwnerKey
        :public RemoteCall
    {
        Method::get_Kdf m_GetKey;

        using RemoteCall::RemoteCall;

        bool GetOwnerKey()
        {
            if (m_This.m_Cache.get_Owner(m_GetKey.m_pPKdf))
            {
                m_Phase += 2;
                return true;
            }

            SendDummyReq();
            m_GetKey.m_Type = KdfType::Root;
            m_This.InvokeAsyncStart(m_GetKey, shared_from_this());

            return false;
        }
    };


    struct RemoteKeyKeeper::Impl::RemoteCall_get_Commitment
        :public RemoteCall_WithOwnerKey
    {
        RemoteCall_get_Commitment(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::get_Commitment& m)
            :RemoteCall_WithOwnerKey(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::get_Commitment& m_M;

        void Update() override
        {
            if (!m_Phase)
            {
                hw::Proto::GetImage::Out msg;
                if (m_M.m_Cid.get_ChildKdfIndex(msg.m_iChild))
                {
                    msg.m_bG = 1;
                    msg.m_bJ = 1;

                    m_M.m_Cid.get_Hash(Cast::Reinterpret<ECC::Hash::Value>(msg.m_hvSrc));

                    SendReq_T(msg);
                }
                else
                {
                    m_Phase += 10;
                    GetOwnerKey();
                    // no return;
                }
            }

            if (1 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::GetImage>();
                if (!pMsg)
                    return;

                ECC::Point::Native ptG, ptJ;
                if (!ptG.ImportNnz(Cast::Reinterpret<ECC::Point>(pMsg->m_ptImageG)) ||
                    !ptJ.ImportNnz(Cast::Reinterpret<ECC::Point>(pMsg->m_ptImageJ)))
                    Fin(Status::Unspecified);

                CoinID::Worker(m_M.m_Cid).Recover(ptG, ptJ);

                ptG.Export(m_M.m_Result);
                Fin();
            }

            if (12 == m_Phase)
            {
                assert(m_GetKey.m_pPKdf);

                ECC::Point::Native comm;
                CoinID::Worker(m_M.m_Cid).Recover(comm, *m_GetKey.m_pPKdf);

                comm.Export(m_M.m_Result);
                Fin();
            }
        }
    };


    struct RemoteKeyKeeper::Impl::RemoteCall_CreateOutput
        :public RemoteCall_WithOwnerKey
    {
        RemoteCall_CreateOutput(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::CreateOutput& m)
            :RemoteCall_WithOwnerKey(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::CreateOutput& m_M;

        Output::Ptr m_pOutput;
        Method::get_Commitment m_GetCommitment;

        void Update() override
        {
            if (!m_Phase)
            {
                if (!Rules::get().IsPastFork_<1>(m_M.m_hScheme))
                {
                    Fin(Status::NotImplemented);
                    return;
                }

                GetOwnerKey();

                SendDummyReq();
                m_GetCommitment.m_Cid = m_M.m_Cid;
                m_This.InvokeAsyncStart(m_GetCommitment, shared_from_this());
            }

            if (4 == m_Phase)
            {
                // have owner key and commitment
                m_pOutput = std::make_unique<Output>();
                m_pOutput->m_Commitment = m_GetCommitment.m_Result;

                // rangeproof
                CalcLocal(Output::OpCode::Mpc_1);

                assert(m_pOutput->m_pConfidential);
                auto& c = *m_pOutput->m_pConfidential;

                hw::Proto::CreateOutput::Out msg;


                CidCvt(msg.m_Cid, m_M.m_Cid);
                msg.m_pT[0] = Ecc2BC(c.m_Part2.m_T1);
                msg.m_pT[1] = Ecc2BC(c.m_Part2.m_T2);

                msg.m_pKExtra[0] = Ecc2BC(m_M.m_User.m_pExtra[0].m_Value);
                msg.m_pKExtra[1] = Ecc2BC(m_M.m_User.m_pExtra[1].m_Value);

                if (m_pOutput->m_pAsset)
                    msg.m_ptAssetGen = Ecc2BC(m_pOutput->m_pAsset->m_hGen);

                SendReq_T(msg);
            }

            if (5 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::CreateOutput>();
                if (!pMsg)
                    return;

                auto& c = *m_pOutput->m_pConfidential;

                Ecc2BC(c.m_Part2.m_T1) = pMsg->m_pT[0];
                Ecc2BC(c.m_Part2.m_T2) = pMsg->m_pT[1];
                Ecc2BC(c.m_Part3.m_TauX.m_Value) = pMsg->m_TauX;

                CalcLocal(Output::OpCode::Mpc_2); // Phase 3

                m_M.m_pResult = std::move(m_pOutput);
                Fin();

            }

        }

        void CalcLocal(Output::OpCode::Enum e)
        {
            ECC::Scalar::Native skDummy;
            ECC::HKdf kdfDummy;

            Asset::Proof::Params::Override po(m_M.m_AidMax);
            m_pOutput->Create(m_M.m_hScheme, skDummy, kdfDummy, m_M.m_Cid, *m_GetKey.m_pPKdf, e, &m_M.m_User); // Phase 3
        }

    };



    struct RemoteKeyKeeper::Impl::RemoteCall_CreateInputShielded
        :public RemoteCall_WithOwnerKey
    {
        Method::CreateInputShielded& m_M;


        Lelantus::Prover m_Prover;
        ECC::Hash::Value m_hvSigmaSeed;
        ECC::Oracle m_Oracle;
        uint32_t m_PtsMsgSent = 0;

        RemoteCall_CreateInputShielded(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::CreateInputShielded& m)
            :RemoteCall_WithOwnerKey(kk, std::move(h))
            ,m_M(m)
            ,m_Prover(*m.m_pList, m.m_pKernel->m_SpendProof)
        {
        }


        void Update() override
        {
            if (!m_Phase)
            {
                GetOwnerKey(); // no return

                static_assert(c_ShieldedInput_ChildKdf == ShieldedTxo::ID::s_iChildOut);

                hw::Proto::GetImage::Out msg;
                msg.m_bG = 1;
                msg.m_bJ = 0;
                msg.m_iChild = c_ShieldedInput_ChildKdf;
                m_M.get_SkOutPreimage(Cast::Reinterpret<ECC::Hash::Value>(msg.m_hvSrc), m_M.m_pKernel->m_Fee);
                SendReq_T(msg);
            }

            if (3 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::GetImage>();
                if (!pMsg)
                    return;

                assert(m_GetKey.m_pPKdf);

                hw::Proto::CreateShieldedInput_1::Out msgOut1;
                hw::Proto::CreateShieldedInput_2::Out msgOut2;
                if (!Setup(*pMsg, msgOut1, msgOut2))
                {
                    Fin(Status::Unspecified);
                    return;
                }

                SendReq_T(msgOut1);

                {
                    ExecutorMT_R exec;
                    Executor::Scope scope(exec);

                    // proof phase1 generation (the most computationally expensive)
                    m_Prover.Generate(m_hvSigmaSeed, m_Oracle, nullptr, Lelantus::Prover::Phase::Step1);
                }

                auto& proof = m_Prover.m_Sigma.m_Proof;

                // Invoke HW device
                msgOut2.m_pABCD[0] = Ecc2BC(proof.m_Part1.m_A);
                msgOut2.m_pABCD[1] = Ecc2BC(proof.m_Part1.m_B);
                msgOut2.m_pABCD[2] = Ecc2BC(proof.m_Part1.m_C);
                msgOut2.m_pABCD[3] = Ecc2BC(proof.m_Part1.m_D);

                SendReq_T(msgOut2);

                const auto& vec = m_Prover.m_Sigma.m_Proof.m_Part1.m_vG;
                uint8_t nMaxPts = 7;

                for (uint32_t nDone = 0; ; )
                {
                    void* pExtra;

                    uint32_t nRemaining = static_cast<uint32_t>(vec.size()) - nDone;
                    if (!nRemaining)
                        break;
                    uint32_t nNaggle = nRemaining;

                    if (nRemaining > nMaxPts)
                    {
                        hw::Proto::CreateShieldedInput_3::Out msgOut3;
                        msgOut3.m_NumPoints = nMaxPts;
                        pExtra = AllocReq_T(msgOut3, sizeof(vec[0]) * nMaxPts);
                        nNaggle = nMaxPts;
                    }
                    else
                    {
                        hw::Proto::CreateShieldedInput_4::Out msgOut4;
                        pExtra = AllocReq_T(msgOut4, sizeof(vec[0]) * nRemaining);
                    }

                    memcpy(pExtra, reinterpret_cast<const uint8_t*>(&vec.front() + nDone), sizeof(vec[0]) * nNaggle);
                    SendReq();

                    nDone += nNaggle;

                    m_Phase--;
                    m_PtsMsgSent++;
                }
            }

            if (6 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::CreateShieldedInput_1>();
                if (!pMsg)
                    return;
            }

            if (7 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::CreateShieldedInput_2>();
                if (!pMsg)
                    return;

                // import SigGen
                auto& proof = Cast::Up<Lelantus::Proof>(m_Prover.m_Sigma.m_Proof);

                Ecc2BC(proof.m_Signature.m_NoncePub) = pMsg->m_NoncePub;
                Ecc2BC(proof.m_Signature.m_pK[0].m_Value) = pMsg->m_SigG;

                // Complete H-part of the generalized Schnorr's signature for output comm
                ECC::Scalar::Native e;
                proof.m_Signature.get_Challenge(e, m_Prover.m_hvSigGen);
                e *= m_M.m_Value;

                ECC::Scalar::Native sH = e + m_Prover.m_Witness.m_R_Output;
                proof.m_Signature.m_pK[1] = -sH;

                if (m_M.m_pKernel->m_pAsset)
                {
                    // Fix G-part (influenced by asset gen blinding)
                    e *= m_Prover.m_Witness.m_R_Adj; // -sH * skAsset

                    e += proof.m_Signature.m_pK[0];
                    proof.m_Signature.m_pK[0] = e;

                }

            }

            while (8 == m_Phase)
            {
                assert(m_PtsMsgSent);

                if (m_PtsMsgSent > 1)
                {
                    auto pMsg = ReadReq_T<hw::Proto::CreateShieldedInput_3>();
                    if (!pMsg)
                        return;

                    m_PtsMsgSent--;
                    m_Phase--;
                }
                else
                {
                    auto pMsg = ReadReq_T<hw::Proto::CreateShieldedInput_4>();
                    if (!pMsg)
                        return;

                    auto& proof = Cast::Up<Lelantus::Proof>(m_Prover.m_Sigma.m_Proof);

                    // import last vG
                    Ecc2BC(proof.m_Part1.m_vG.back()) = pMsg->m_G_Last;
                    Ecc2BC(proof.m_Part2.m_zR.m_Value) = pMsg->m_zR;

                    // phase2
                    m_Prover.Generate(m_hvSigmaSeed, m_Oracle, nullptr, Lelantus::Prover::Phase::Step2);

                    // finished
                    m_M.m_pKernel->MsgToID();
                    Fin();
                    return;
                }
            }

        }

        bool Setup(const hw::Proto::GetImage::In& msgIn, hw::Proto::CreateShieldedInput_1::Out& msgOut1, hw::Proto::CreateShieldedInput_2::Out& msgOut2)
        {
            auto& m = m_M; // alias
            assert(m.m_pList && m.m_pKernel);
            auto& krn = *m.m_pKernel; // alias

            ShieldedTxo::Data::Params pars;
            pars.Set(*m_GetKey.m_pPKdf, m);
            ShieldedTxo::Data::Params::Plus plus(pars);

            Import(msgOut1.m_InpBlob, msgOut1.m_InpFmt, m, krn.m_Fee);

            msgOut1.m_SpendParams.m_hMin = krn.m_Height.m_Min;
            msgOut1.m_SpendParams.m_hMax = krn.m_Height.m_Max;
            msgOut1.m_SpendParams.m_WindowEnd = krn.m_WindowEnd;
            msgOut1.m_SpendParams.m_Sigma_M = krn.m_SpendProof.m_Cfg.M;
            msgOut1.m_SpendParams.m_Sigma_n = krn.m_SpendProof.m_Cfg.n;

            Lelantus::Proof& proof = krn.m_SpendProof;

            m_Prover.m_Witness.m_V = 0; // not used
            m_Prover.m_Witness.m_L = m.m_iIdx;
            m_Prover.m_Witness.m_R = plus.m_skFull;

            // output commitment
            ECC::Point::Native comm;
            if (!comm.ImportNnz(Cast::Reinterpret<ECC::Point>(msgIn.m_ptImageG)))
                return false;
            
            ECC::Tag::AddValue(comm, &plus.m_hGen, m.m_Value);

            proof.m_Commitment = comm;
            proof.m_SpendPk = pars.m_Ticket.m_SpendPk;

            Asset::Proof::Params::Override po(m_M.m_AidMax);

            if (Asset::Proof::Params::IsNeeded(m.m_AssetID, krn.m_Height.m_Min))
            {
                ECC::Hash::Processor()
                    << "asset-blind.sh"
                    << proof.m_Commitment // pseudo-uniquely indentifies this specific Txo
                    >> m_hvSigmaSeed;

                m_GetKey.m_pPKdf->DerivePKey(m_Prover.m_Witness.m_R_Adj, m_hvSigmaSeed);

                krn.m_pAsset = std::make_unique<Asset::Proof>();
                krn.m_pAsset->Create(krn.m_Height.m_Min, plus.m_hGen, m_Prover.m_Witness.m_R_Adj, m.m_AssetID, plus.m_hGen);
            }

            {
                ECC::Hash::Processor()
                    << "h-blind.sh"
                    << proof.m_Commitment
                    >> m_hvSigmaSeed;

                m_GetKey.m_pPKdf->DerivePKey(m_Prover.m_Witness.m_R_Output, m_hvSigmaSeed);

                ECC::Mode::Scope scope(ECC::Mode::Fast);

                comm = Zero;
                if (krn.m_pAsset)
                    comm = plus.m_hGen * m_Prover.m_Witness.m_R_Output;
                else
                    comm = ECC::Context::get().H * m_Prover.m_Witness.m_R_Output;

                comm.Export(Cast::Reinterpret<ECC::Point>(msgOut2.m_NoncePub));
            }

            krn.UpdateMsg();
            m_Oracle << krn.m_Msg;

            if (Rules::get().IsPastFork_<3>(krn.m_Height.m_Min))
            {
                m_Oracle << krn.m_NotSerialized.m_hvShieldedState;
                Asset::Proof::Expose(m_Oracle, krn.m_Height.m_Min, krn.m_pAsset);

                msgOut1.m_ShieldedState = Ecc2BC(krn.m_NotSerialized.m_hvShieldedState);

                if (krn.m_pAsset)
                    msgOut1.m_ptAssetGen = Ecc2BC(krn.m_pAsset->m_hGen);
            }

            // generate seed for Sigma proof blinding. Use mix of deterministic + random params
            //ECC::GenRandom(m_hvSigmaSeed);
            ECC::Hash::Processor()
                << "seed.sigma.sh"
                << m_hvSigmaSeed
                << krn.m_Msg
                << proof.m_Commitment
                >> m_hvSigmaSeed;

            return true;
        }

    };



    struct RemoteKeyKeeper::Impl::RemoteCall_CreateVoucherShielded
        :public RemoteCall
    {
        RemoteCall_CreateVoucherShielded(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::CreateVoucherShielded& m)
            :RemoteCall(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::CreateVoucherShielded& m_M;
        std::vector<ShieldedTxo::Voucher> m_vRes;
        uint8_t m_AuxDone;

        void ImportVoucher(const void* pData, uint32_t iIdx)
        {
            const auto& src = *reinterpret_cast<const hw::ShieldedVoucher*>(pData);

            assert(iIdx < m_vRes.size());
            ShieldedTxo::Voucher& trg = m_vRes[iIdx];

            Ecc2BC(trg.m_Ticket.m_SerialPub) = src.m_SerialPub;
            Ecc2BC(trg.m_Ticket.m_Signature.m_NoncePub) = src.m_NoncePub;

            static_assert(_countof(trg.m_Ticket.m_Signature.m_pK) == _countof(src.m_pK));
            for (uint32_t iK = 0; iK < static_cast<uint32_t>(_countof(src.m_pK)); iK++)
                Ecc2BC(trg.m_Ticket.m_Signature.m_pK[iK].m_Value) = src.m_pK[iK];

            Ecc2BC(trg.m_SharedSecret) = src.m_SharedSecret;
            Ecc2BC(trg.m_Signature.m_NoncePub) = src.m_Signature.m_NoncePub;
            Ecc2BC(trg.m_Signature.m_k.m_Value) = src.m_Signature.m_k;
        }

        void Update() override
        {
            if (!m_Phase)
            {
                if (!m_M.m_Count)
                {
                    Fin();
                    return;
                }

                const uint8_t nMaxAux = sizeof(hw::KeyKeeper_AuxBuf) / sizeof(hw::ShieldedVoucher);
                uint32_t nCount = std::min<uint32_t>(m_M.m_Count, nMaxAux + 1);

                hw::Proto::CreateShieldedVouchers::Out msg;

                msg.m_Count = (uint8_t) nCount;
                msg.m_Nonce0 = Ecc2BC(m_M.m_Nonce);
                msg.m_AddrID = m_M.m_iEndpoint;

                SendReq_T(msg, sizeof(hw::ShieldedVoucher));

                m_vRes.resize(nCount);
                m_AuxDone = 0;
            }

            if (1 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::CreateShieldedVouchers>();
                if (!pMsg)
                    return;

                ImportVoucher(pMsg + 1, static_cast<uint32_t>(m_vRes.size() - 1));

                m_Phase = 10;
            }

            if (11 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::AuxRead>();
                if (!pMsg)
                    return;

                ImportVoucher(pMsg + 1, m_AuxDone++);
                m_Phase = 10;
            }

            if (10 == m_Phase)
            {
                if (m_AuxDone == m_vRes.size() - 1)
                {
                    m_M.m_Res = std::move(m_vRes);
                    Fin();
                }
                else
                {
                    hw::Proto::AuxRead::Out msg;
                    msg.m_Size = sizeof(hw::ShieldedVoucher);
                    msg.m_Offset = sizeof(hw::ShieldedVoucher) * m_AuxDone;

                    SendReq_T(msg, sizeof(hw::ShieldedVoucher));
                }
            }

        }

    };

    struct RemoteKeyKeeper::Impl::RemoteCall_CreateOfflineAddr
        :public RemoteCall_WithOwnerKey
    {
        RemoteCall_CreateOfflineAddr(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::CreateOfflineAddr& m)
            :RemoteCall_WithOwnerKey(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::CreateOfflineAddr& m_M;

        void Update() override
        {
            if (!m_Phase)
            {
                GetOwnerKey();

                hw::Proto::SignOfflineAddr::Out msg;
                msg.m_AddrID = m_M.m_iEndpoint;
                SendReq_T(msg);
            }

            if (3 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::SignOfflineAddr>();
                if (!pMsg)
                    return;

                ShieldedTxo::Viewer viewer;
                viewer.FromOwner(*m_GetKey.m_pPKdf, 0);

                m_M.m_Addr.FromViewer(viewer);
                Ecc2BC(m_M.m_Signature.m_NoncePub) = pMsg->m_Signature.m_NoncePub;
                Ecc2BC(m_M.m_Signature.m_k.m_Value) = pMsg->m_Signature.m_k;

                Fin();
            }
        }
    };

    struct RemoteKeyKeeper::Impl::RemoteCall_WithCoins
        :public RemoteCall
    {
        using RemoteCall::RemoteCall;

        static const uint32_t s_CoinsSent = 100000;

        uint32_t m_AcksPending = 0;

        void SendCoins(const Method::InOuts& m)
        {
            m_Phase += s_CoinsSent;

            size_t nDoneIns = 0, nDoneOuts = 0, nDoneInsShielded = 0;

            hw::Proto::TxAddCoins::Out msg;
            for (msg.m_Reset = 1; ; msg.m_Reset = 0)
            {
                const uint32_t nMaxSize = 240;
                const uint32_t nSizeInpShielded = sizeof(hw::ShieldedInput_Blob) + sizeof(hw::ShieldedInput_Fmt);
                static_assert(nMaxSize >= sizeof(hw::CoinID));
                static_assert(nMaxSize >= nSizeInpShielded);

                uint32_t nReserve = nMaxSize;

                msg.m_Ins = (uint8_t) std::min<size_t>(m.m_vInputs.size() - nDoneIns, nReserve / sizeof(hw::CoinID));
                nReserve -= sizeof(hw::CoinID) * msg.m_Ins;

                msg.m_Outs = (uint8_t) std::min<size_t>(m.m_vOutputs.size() - nDoneOuts, nReserve / sizeof(hw::CoinID));
                nReserve -= sizeof(hw::CoinID) * msg.m_Outs;

                msg.m_InsShielded = (uint8_t) std::min<size_t>(m.m_vInputsShielded.size() - nDoneInsShielded, nReserve / nSizeInpShielded);
                nReserve -= nSizeInpShielded * msg.m_InsShielded;


                uint32_t nExtra = nMaxSize - nReserve;
                void* pExtra = AllocReq_T(msg, nExtra);

                hw::CoinID* pCid = (hw::CoinID*) pExtra;

                for (uint32_t i = 0; i < msg.m_Ins; i++, pCid++)
                {
                    CidCvt(*pCid, m.m_vInputs[nDoneIns++]);
                    hw::Proto::h2n(*pCid);
                }

                for (uint32_t i = 0; i < msg.m_Outs; i++, pCid++)
                {
                    CidCvt(*pCid, m.m_vOutputs[nDoneOuts++]);
                    hw::Proto::h2n(*pCid);
                }

                auto pPtr = reinterpret_cast<uint8_t*>(pCid);
                for (uint32_t i = 0; i < msg.m_InsShielded; i++)
                {
                    const auto& src = m.m_vInputsShielded[nDoneInsShielded++];

                    hw::ShieldedInput_Fmt fmt;
                    Import(*(hw::ShieldedInput_Blob*)pPtr, fmt, src, src.m_Fee);
                    hw::Proto::h2n(fmt);

                    pPtr += sizeof(hw::ShieldedInput_Blob);
                    memcpy(pPtr, &fmt, sizeof(fmt));
                    pPtr += sizeof(hw::ShieldedInput_Fmt);
                }

                m_Phase--;
                SendReq();
                m_AcksPending++;

                if ((nDoneIns == m.m_vInputs.size()) && (nDoneOuts == m.m_vOutputs.size()) && (nDoneInsShielded == m.m_vInputsShielded.size()))
                    break;
            }

        }

        bool ReadCoinsAck()
        {
            for (; m_AcksPending; m_AcksPending--, m_Phase--)
            {
                auto pMsg = ReadReq_T<hw::Proto::TxAddCoins>();
                if (!pMsg)
                    return false;
            }

            return true;
        }
    };



    struct RemoteKeyKeeper::Impl::RemoteCall_SignReceiver
        :public RemoteCall_WithCoins
    {
        RemoteCall_SignReceiver(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::SignReceiver& m)
            :RemoteCall_WithCoins(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::SignReceiver& m_M;

        void Update() override
        {
            if (!m_Phase)
            {
                SendCoins(m_M);

                hw::Proto::TxReceive::Out msg;
                Import(msg.m_Tx.m_Krn, m_M);
                Import(msg.m_Comms, m_M);
                Import(msg.m_Mut, m_M);

                SendReq_T(msg);
            }

            if (!ReadCoinsAck())
                return;

            if (s_CoinsSent + 1 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::TxReceive>();
                if (!pMsg)
                    return;

                Export(m_M, pMsg->m_Tx);

                Ecc2BC(m_M.m_PaymentProofSignature.m_NoncePub) = pMsg->m_PaymentProof.m_NoncePub;
                Ecc2BC(m_M.m_PaymentProofSignature.m_k.m_Value) = pMsg->m_PaymentProof.m_k;
                Fin();
            }

        }

    };

    struct RemoteKeyKeeper::Impl::RemoteCall_SignSender
        :public RemoteCall_WithCoins
    {
        RemoteCall_SignSender(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::SignSender& m)
            :RemoteCall_WithCoins(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::SignSender& m_M;
        bool m_Initial;

        void Update() override
        {
            if (!m_Phase)
            {
                // send coins each time. Assume the remote could be busy doing something else in the meanwhile
                SendCoins(m_M);

                m_Initial = (m_M.m_UserAgreement == Zero);

                if (m_Initial)
                {
                    hw::Proto::TxSend1::Out msg;
                    msg.m_iSlot = m_M.m_Slot;
                    Import(msg.m_Mut, m_M);
                    Import(msg.m_Tx.m_Krn, m_M);

                    SendReq_T(msg);
                }
                else
                {
                    hw::Proto::TxSend2::Out msg;
                    msg.m_iSlot = m_M.m_Slot;
                    Import(msg.m_Tx.m_Krn, m_M);
                    Import(msg.m_Mut, m_M);

                    msg.m_PaymentProof.m_NoncePub = Ecc2BC(m_M.m_PaymentProofSignature.m_NoncePub);
                    msg.m_PaymentProof.m_k = Ecc2BC(m_M.m_PaymentProofSignature.m_k.m_Value);

                    msg.m_UserAgreement = Ecc2BC(m_M.m_UserAgreement);
                    Import(msg.m_Comms, m_M);

                    SendReq_T(msg);
                }
            }

            if (!ReadCoinsAck())
                return;

            if (s_CoinsSent + 1 == m_Phase)
            {
                auto& krn = *m_M.m_pKernel;

                if (m_Initial)
                {
                    auto pMsg = ReadReq_T<hw::Proto::TxSend1>();
                    if (!pMsg)
                        return;

                    Ecc2BC(m_M.m_UserAgreement) = pMsg->m_UserAgreement;
                    Ecc2BC(krn.m_Commitment) = pMsg->m_Comms.m_Commitment;
                    Ecc2BC(krn.m_Signature.m_NoncePub) = pMsg->m_Comms.m_NoncePub;

                    krn.UpdateID(); // not really required, the kernel isn't full yet
                }
                else
                {
                    auto pMsg = ReadReq_T<hw::Proto::TxSend2>();
                    if (!pMsg)
                        return;

                    // add scalars
                    ECC::Scalar k_;
                    Ecc2BC(k_.m_Value) = pMsg->m_TxSig.m_kSig;

                    ECC::Scalar::Native k(krn.m_Signature.m_k);
                    k += k_;
                    krn.m_Signature.m_k = k;

                    Ecc2BC(k_.m_Value) = pMsg->m_TxSig.m_kOffset;
                    m_M.m_kOffset += k_;
                }

                Fin();

            }

        }

    };

    struct RemoteKeyKeeper::Impl::RemoteCall_SignSendShielded
        :public RemoteCall_WithCoins
    {
        RemoteCall_SignSendShielded(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::SignSendShielded& m)
            :RemoteCall_WithCoins(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::SignSendShielded& m_M;
        TxKernelShieldedOutput::Ptr m_pOutp;
        uint32_t m_BlobsSent = 0;

        void Update() override
        {
            if (!m_Phase)
            {
                SendCoins(m_M);

                hw::Proto::TxSendShielded::Out msg;

                msg.m_Mut.m_Peer = Ecc2BC(m_M.m_Peer);
                msg.m_Mut.m_AddrID = m_M.m_iEndpoint;
                Import(msg.m_User, m_M.m_User);
                Import(msg.m_Tx.m_Krn, m_M);

                hw::ShieldedOutParams sop;
                ZeroObject(sop.u);

                msg.m_UsePublicGen = !m_M.m_pVoucher;

                ShieldedTxo::Voucher voucherInst;
                auto& voucher = m_M.m_pVoucher ? *m_M.m_pVoucher : voucherInst;

                if (m_M.m_pVoucher)
                {
                    sop.u.m_Voucher.m_SerialPub = Ecc2BC(voucher.m_Ticket.m_SerialPub);
                    sop.u.m_Voucher.m_NoncePub = Ecc2BC(voucher.m_Ticket.m_Signature.m_NoncePub);
                    sop.u.m_Voucher.m_pK[0] = Ecc2BC(voucher.m_Ticket.m_Signature.m_pK[0].m_Value);
                    sop.u.m_Voucher.m_pK[1] = Ecc2BC(voucher.m_Ticket.m_Signature.m_pK[1].m_Value);
                    sop.u.m_Voucher.m_SharedSecret = Ecc2BC(voucher.m_SharedSecret);
                    sop.u.m_Voucher.m_Signature.m_NoncePub = Ecc2BC(voucher.m_Signature.m_NoncePub);
                    sop.u.m_Voucher.m_Signature.m_k = Ecc2BC(voucher.m_Signature.m_k.m_Value);
                }
                else
                {
                    if (!m_M.m_pOffline)
                    {
                        Fin(Status::Unspecified);
                        return;
                    }

                    auto& off = *m_M.m_pOffline;

                    sop.u.m_Offline.m_Nonce = Ecc2BC(off.m_Nonce);
                    sop.u.m_Offline.m_Sig.m_NoncePub = Ecc2BC(off.m_Signature.m_NoncePub);
                    sop.u.m_Offline.m_Sig.m_k = Ecc2BC(off.m_Signature.m_k.m_Value);

                    ShieldedTxo::PublicGen::Packed p;
                    off.m_Addr.Export(p);

                    sop.u.m_Offline.m_Addr.m_Gen_Secret = Ecc2BC(p.m_Gen.m_Secret);
                    sop.u.m_Offline.m_Addr.m_Ser_Secret = Ecc2BC(p.m_Ser.m_Secret);
                    sop.u.m_Offline.m_Addr.m_Gen_PkG = Ecc2BC(p.m_Gen.m_PkG);
                    sop.u.m_Offline.m_Addr.m_Gen_PkJ = Ecc2BC(p.m_Gen.m_PkJ);
                    sop.u.m_Offline.m_Addr.m_Ser_PkG = Ecc2BC(p.m_Ser.m_PkG);

                    ShieldedTxo::Data::TicketParams tp;
                    tp.Generate(voucher.m_Ticket, off.m_Addr, off.m_Nonce);
                    voucher.m_SharedSecret = tp.m_SharedSecret;
                }

                ShieldedTxo::Data::OutputParams op;

                // the amount and asset are not directly specified, they must be deduced from tx balance.
                // Don't care about value overflow, or asset ambiguity, this will be re-checked by the HW wallet anyway.

                op.m_Value = CalcTxBalance(nullptr, m_M);
                op.m_Value -= msg.m_Tx.m_Krn.m_Fee;

                if (op.m_Value)
                    op.m_AssetID = 0;
                else
                    // no net value transferred in beams, try assets
                    op.m_Value = CalcTxBalance(&op.m_AssetID, m_M);

                op.m_User = m_M.m_User;
                op.Restore_kG(voucher.m_SharedSecret);

                Asset::Proof::Params::Override po(m_M.m_AidMax);

                m_pOutp = std::make_unique<TxKernelShieldedOutput>();
                TxKernelShieldedOutput& krn1 = *m_pOutp;

                krn1.m_CanEmbed = true;
                krn1.m_Txo.m_Ticket = voucher.m_Ticket;

                krn1.UpdateMsg();
                ECC::Oracle oracle;
                oracle << krn1.m_Msg;

                op.Generate(krn1.m_Txo, voucher.m_SharedSecret, m_M.m_pKernel->m_Height.m_Min, oracle);
                krn1.MsgToID();

                msg.m_HideAssetAlways = !!krn1.m_Txo.m_pAsset;

                if (krn1.m_Txo.m_pAsset)
                    msg.m_ptAssetGen = Ecc2BC(krn1.m_Txo.m_pAsset->m_hGen);

                SerializerIntoStaticBuf ser(&sop.m_RangeProof);
                ser & krn1.m_Txo.m_RangeProof;
                assert(ser.get_Size(&sop.m_RangeProof) == sizeof(sop.m_RangeProof));

                for (uint32_t nDone = 0; nDone < sizeof(sop); )
                {
                    uint32_t nPortion = std::min<uint32_t>(230, sizeof(sop) - nDone);

                    hw::Proto::AuxWrite::Out msgPrep;
                    msgPrep.m_Offset = (uint16_t) nDone;
                    msgPrep.m_Size = (uint16_t) nPortion;
                    
                    auto pExtra = AllocReq_T(msgPrep, nPortion);
                    memcpy(pExtra, reinterpret_cast<const uint8_t*>(&sop) + nDone, nPortion);
                    nDone += nPortion;

                    SendReq();

                    m_Phase--;
                    m_BlobsSent++;
                }


                SendReq_T(msg);
            }

            if (!ReadCoinsAck())
                return;

            while (s_CoinsSent + 1 == m_Phase)
            {
                if (m_BlobsSent)
                {
                    auto pMsg = ReadReq_T<hw::Proto::AuxWrite>();
                    if (!pMsg)
                        return;

                    m_BlobsSent--;
                    m_Phase--;
                }
                else
                {
                    auto pMsg = ReadReq_T<hw::Proto::TxSendShielded>();
                    if (!pMsg)
                        return;

                    m_M.m_pKernel->m_vNested.push_back(std::move(m_pOutp));
                    Export(m_M, pMsg->m_Tx);

                    Fin();
                    return;
                }
            }


        }

    };

    struct RemoteKeyKeeper::Impl::RemoteCall_SignSplit
        :public RemoteCall_WithCoins
    {
        RemoteCall_SignSplit(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::SignSplit& m)
            :RemoteCall_WithCoins(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::SignSplit& m_M;

        void Update() override
        {
            if (!m_Phase)
            {
                SendCoins(m_M);

                hw::Proto::TxSplit::Out msg;
                Import(msg.m_Tx.m_Krn, m_M);
                SendReq_T(msg);
            }

            if (!ReadCoinsAck())
                return;

            if (s_CoinsSent + 1 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::TxSplit>();
                if (!pMsg)
                    return;

                Export(m_M, pMsg->m_Tx);

                Fin();
            }
        }

    };

    struct RemoteKeyKeeper::Impl::RemoteCall_DisplayEndpoint
        :public RemoteCall
    {
        RemoteCall_DisplayEndpoint(RemoteKeyKeeper& kk, Handler::Ptr&& h, Method::DisplayEndpoint& m)
            :RemoteCall(kk, std::move(h))
            ,m_M(m)
        {
        }

        Method::DisplayEndpoint& m_M;

        void Update() override
        {
            if (!m_Phase)
            {
                hw::Proto::DisplayEndpoint::Out msg;
                msg.m_AddrID = m_M.m_iEndpoint;
                SendReq_T(msg);
            }

            if (1 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::DisplayEndpoint>();
                if (!pMsg)
                    return;

                Fin();
            }
        }

    };


#define THE_MACRO(method) \
    void RemoteKeyKeeper::InvokeAsyncStart(Method::method& m, Handler::Ptr&& h) \
    { \
        auto pCall = std::make_shared<Impl::RemoteCall_##method>(*this, std::move(h), m); \
        pCall->Update(); \
    } \
 \
    void RemoteKeyKeeper::InvokeAsync(Method::method& m, const Handler::Ptr& h) \
    { \
        if (m_InProgress) \
        { \
            struct MyPending :public Pending \
            { \
                Method::method* m_pMethod; \
                void Start(RemoteKeyKeeper& kk) override  { kk.InvokeAsyncStart(*m_pMethod, std::move(m_pHandler)); } \
            }; \
            MyPending* p = new MyPending; \
            m_lstPending.push_back(*p); \
            p->m_pMethod = &m; \
            p->m_pHandler = h; \
        } \
        else \
            InvokeAsyncStart(m, Handler::Ptr(h)); \
    }

    KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

    void RemoteKeyKeeper::CheckPending()
    {
        while (!m_lstPending.empty() && !m_InProgress)
        {
            auto& x = m_lstPending.front();
            std::unique_ptr<Pending> pGuard(&x);
            m_lstPending.pop_front();

            assert(x.m_pHandler);
            //if (x.m_pHandler.use_count() > 1) // already cancelled?
            {
                struct RecursionPreventor {
                    uint32_t& m_Var;
                    RecursionPreventor(uint32_t& var) :m_Var(var) { m_Var++;  }
                    ~RecursionPreventor() { m_Var--; }

                } rp(m_InProgress);

                x.Start(*this);
            }
        }
    }
}

