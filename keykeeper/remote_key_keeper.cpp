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
        std::unique_lock<std::mutex> scope(m_Mutex);

        pRes = m_pOwner;
        return pRes != nullptr;
    }

    void RemoteKeyKeeper::Cache::set_Owner(const Key::IPKdf::Ptr& pRes)
    {
        std::unique_lock<std::mutex> scope(m_Mutex);

        if (!m_pOwner)
            m_pOwner = pRes;
    }

    uint32_t RemoteKeyKeeper::Cache::get_NumSlots()
    {
        std::unique_lock<std::mutex> scope(m_Mutex);
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

            RemoteCall(RemoteKeyKeeper& kk, const Handler::Ptr& h)
                :m_This(kk)
                ,m_pFinal(h)
            {
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

            void Fin(Status::Type n = Status::Success) {
                m_This.PushOut(n, m_pFinal);
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


#define THE_MACRO(method) struct RemoteCall_##method;
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
	    txIn.m_AddrID = m.m_MyIDKey;
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
        RemoteCall_get_Kdf(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::get_Kdf& m)
            :RemoteCall(kk, h)
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
        RemoteCall_get_NumSlots(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::get_NumSlots& m)
            :RemoteCall(kk, h)
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
                    std::unique_lock<std::mutex> scope(m_This.m_Cache.m_Mutex);
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
            m_This.InvokeAsync(m_GetKey, shared_from_this());

            return false;
        }
    };


    struct RemoteKeyKeeper::Impl::RemoteCall_get_Commitment
        :public RemoteCall_WithOwnerKey
    {
        RemoteCall_get_Commitment(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::get_Commitment& m)
            :RemoteCall_WithOwnerKey(kk, h)
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
        RemoteCall_CreateOutput(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::CreateOutput& m)
            :RemoteCall_WithOwnerKey(kk, h)
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
                if (m_M.m_hScheme < Rules::get().pForks[1].m_Height)
                {
                    Fin(Status::NotImplemented);
                    return;
                }

                GetOwnerKey();

                SendDummyReq();
                m_GetCommitment.m_Cid = m_M.m_Cid;
                m_This.InvokeAsync(m_GetCommitment, shared_from_this());
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

        RemoteCall_CreateInputShielded(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::CreateInputShielded& m)
            :RemoteCall_WithOwnerKey(kk, h)
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

                const auto& vec = m_Prover.m_Sigma.m_Proof.m_Part1.m_vG;
                size_t nSize = sizeof(vec[0]) * vec.size();

                void* pExtra = AllocReq_T(msgOut2, (uint32_t) nSize);
                if (nSize)
                    memcpy(pExtra, reinterpret_cast<const uint8_t*>(&vec.front()), nSize);

                SendReq();
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

                auto& proof = Cast::Up<Lelantus::Proof>(m_Prover.m_Sigma.m_Proof);

                // import SigGen and vG[0]
                Ecc2BC(proof.m_Part1.m_vG.front()) = pMsg->m_G0;
                Ecc2BC(proof.m_Part2.m_zR.m_Value) = pMsg->m_zR;

                Ecc2BC(proof.m_Signature.m_NoncePub) = pMsg->m_NoncePub;
                Ecc2BC(proof.m_Signature.m_pK[0].m_Value) = pMsg->m_pSig[0];
                Ecc2BC(proof.m_Signature.m_pK[1].m_Value) = pMsg->m_pSig[1];

                // phase2
                m_Prover.Generate(m_hvSigmaSeed, m_Oracle, nullptr, Lelantus::Prover::Phase::Step2);

                if (m_M.m_pKernel->m_pAsset)
                {
                    // Fix signature (G-part)
                    ECC::Scalar::Native k = proof.m_Signature.m_pK[1];
                    k *= m_Prover.m_Witness.m_R_Adj;

                    k = -k;
                    k += proof.m_Signature.m_pK[0];
                    proof.m_Signature.m_pK[0] = k;
                }


                // finished
                m_M.m_pKernel->MsgToID();
                Fin();

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

            bool bHideAssetAlways = false; // TODO - parameter
            if (bHideAssetAlways || m.m_AssetID)
            {
                ECC::Hash::Processor()
                    << "asset-blind.sh"
                    << proof.m_Commitment // pseudo-uniquely indentifies this specific Txo
                    >> m_hvSigmaSeed;

                m_GetKey.m_pPKdf->DerivePKey(m_Prover.m_Witness.m_R_Adj, m_hvSigmaSeed);

                krn.m_pAsset = std::make_unique<Asset::Proof>();
                krn.m_pAsset->Create(plus.m_hGen, m_Prover.m_Witness.m_R_Adj, m.m_AssetID, plus.m_hGen);
            }

            krn.UpdateMsg();
            m_Oracle << krn.m_Msg;

            if (krn.m_Height.m_Min >= Rules::get().pForks[3].m_Height)
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
        RemoteCall_CreateVoucherShielded(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::CreateVoucherShielded& m)
            :RemoteCall(kk, h)
            ,m_M(m)
        {
        }

        Method::CreateVoucherShielded& m_M;

        uint32_t m_MaxCount;

        void Update() override
        {
            if (!m_Phase)
            {
                if (!m_M.m_Count)
                {
                    Fin();
                    return;
                }

                m_MaxCount = std::min(m_M.m_Count, 30U);
                size_t nSize = sizeof(hw::ShieldedVoucher) * m_MaxCount;

                hw::Proto::CreateShieldedVouchers::Out msg;

                msg.m_Count = m_MaxCount;
                msg.m_Nonce0 = Ecc2BC(m_M.m_Nonce);
                msg.m_AddrID = m_M.m_MyIDKey;

                SendReq_T(msg, (uint32_t) nSize);
            }

            if (1 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::CreateShieldedVouchers>();
                if (!pMsg)
                    return;

                if (!pMsg->m_Count || (pMsg->m_Count > m_MaxCount))
                {
                    Fin(c_KeyKeeper_Status_ProtoError);
                    return;
                }

                auto* pRes = reinterpret_cast<hw::ShieldedVoucher*>(pMsg + 1);

                m_M.m_Res.resize(pMsg->m_Count);
                for (uint32_t i = 0; i < pMsg->m_Count; i++)
                {
                    const hw::ShieldedVoucher& src = pRes[i];
                    ShieldedTxo::Voucher& trg = m_M.m_Res[i];

                    Ecc2BC(trg.m_Ticket.m_SerialPub) = src.m_SerialPub;
                    Ecc2BC(trg.m_Ticket.m_Signature.m_NoncePub) = src.m_NoncePub;

                    static_assert(_countof(trg.m_Ticket.m_Signature.m_pK) == _countof(src.m_pK));
                    for (uint32_t iK = 0; iK < static_cast<uint32_t>(_countof(src.m_pK)); iK++)
                        Ecc2BC(trg.m_Ticket.m_Signature.m_pK[iK].m_Value) = src.m_pK[iK];

                    Ecc2BC(trg.m_SharedSecret) = src.m_SharedSecret;
                    Ecc2BC(trg.m_Signature.m_NoncePub) = src.m_Signature.m_NoncePub;
                    Ecc2BC(trg.m_Signature.m_k.m_Value) = src.m_Signature.m_k;
                }

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

            // TODO: multiple invocations if necessary

            hw::Proto::TxAddCoins::Out msg;

            msg.m_Reset = 1;
            msg.m_Ins = static_cast<uint8_t>(m.m_vInputs.size());
            msg.m_Outs = static_cast<uint8_t>(m.m_vOutputs.size());
            msg.m_InsShielded = static_cast<uint8_t>(m.m_vInputsShielded.size());

            uint32_t nExtra =
                static_cast <uint32_t>(sizeof(hw::CoinID)) * (msg.m_Ins + (uint32_t)msg.m_Outs) +
                static_cast <uint32_t>(sizeof(hw::ShieldedInput_Blob) + sizeof(hw::ShieldedInput_Fmt)) * msg.m_InsShielded;

            void* pExtra = AllocReq_T(msg, nExtra);

            hw::CoinID* pCid = (hw::CoinID*) pExtra;

            for (uint32_t i = 0; i < msg.m_Ins; i++, pCid++)
            {
                CidCvt(*pCid, m.m_vInputs[i]);
                hw::Proto::h2n(*pCid);
            }

            for (uint32_t i = 0; i < msg.m_Outs; i++, pCid++)
            {
                CidCvt(*pCid, m.m_vOutputs[i]);
                hw::Proto::h2n(*pCid);
            }

            auto pPtr = reinterpret_cast<uint8_t*>(pCid);
            for (uint32_t i = 0; i < msg.m_InsShielded; i++)
            {
                const auto& src = m.m_vInputsShielded[i];

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
        RemoteCall_SignReceiver(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::SignReceiver& m)
            :RemoteCall_WithCoins(kk, h)
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
        RemoteCall_SignSender(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::SignSender& m)
            :RemoteCall_WithCoins(kk, h)
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
        RemoteCall_SignSendShielded(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::SignSendShielded& m)
            :RemoteCall_WithCoins(kk, h)
            ,m_M(m)
        {
        }

        Method::SignSendShielded& m_M;
        TxKernelShieldedOutput::Ptr m_pOutp;

        void Update() override
        {
            if (!m_Phase)
            {
                SendCoins(m_M);

                hw::Proto::TxSendShielded::Out msg;

                msg.m_Mut.m_Peer = Ecc2BC(m_M.m_Peer);
                msg.m_Mut.m_AddrID = m_M.m_MyIDKey;
                msg.m_HideAssetAlways = m_M.m_HideAssetAlways;
                Import(msg.m_User, m_M.m_User);
                Import(msg.m_Tx.m_Krn, m_M);

                msg.m_Voucher.m_SerialPub = Ecc2BC(m_M.m_Voucher.m_Ticket.m_SerialPub);
                msg.m_Voucher.m_NoncePub = Ecc2BC(m_M.m_Voucher.m_Ticket.m_Signature.m_NoncePub);
                msg.m_Voucher.m_pK[0] = Ecc2BC(m_M.m_Voucher.m_Ticket.m_Signature.m_pK[0].m_Value);
                msg.m_Voucher.m_pK[1] = Ecc2BC(m_M.m_Voucher.m_Ticket.m_Signature.m_pK[1].m_Value);
                msg.m_Voucher.m_SharedSecret = Ecc2BC(m_M.m_Voucher.m_SharedSecret);
                msg.m_Voucher.m_Signature.m_NoncePub = Ecc2BC(m_M.m_Voucher.m_Signature.m_NoncePub);
                msg.m_Voucher.m_Signature.m_k = Ecc2BC(m_M.m_Voucher.m_Signature.m_k.m_Value);

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
                op.Restore_kG(m_M.m_Voucher.m_SharedSecret);

                m_pOutp = std::make_unique<TxKernelShieldedOutput>();
                TxKernelShieldedOutput& krn1 = *m_pOutp;

                krn1.m_CanEmbed = true;
                krn1.m_Txo.m_Ticket = m_M.m_Voucher.m_Ticket;

                krn1.UpdateMsg();
                ECC::Oracle oracle;
                oracle << krn1.m_Msg;

                op.Generate(krn1.m_Txo, m_M.m_Voucher.m_SharedSecret, m_M.m_pKernel->m_Height.m_Min, oracle, m_M.m_HideAssetAlways);
                krn1.MsgToID();

                if (krn1.m_Txo.m_pAsset)
                    msg.m_ptAssetGen = Ecc2BC(krn1.m_Txo.m_pAsset->m_hGen);

                SerializerIntoStaticBuf ser(&msg.m_RangeProof);
                ser& krn1.m_Txo.m_RangeProof;
                assert(ser.get_Size(&msg.m_RangeProof) == sizeof(msg.m_RangeProof));

                SendReq_T(msg);
            }

            if (!ReadCoinsAck())
                return;

            if (s_CoinsSent + 1 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::TxSendShielded>();
                if (!pMsg)
                    return;

                m_M.m_pKernel->m_vNested.push_back(std::move(m_pOutp));
                Export(m_M, pMsg->m_Tx);

                Fin();
            }


        }

    };

    struct RemoteKeyKeeper::Impl::RemoteCall_SignSplit
        :public RemoteCall_WithCoins
    {
        RemoteCall_SignSplit(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::SignSplit& m)
            :RemoteCall_WithCoins(kk, h)
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

    struct RemoteKeyKeeper::Impl::RemoteCall_DisplayWalletID
        :public RemoteCall
    {
        RemoteCall_DisplayWalletID(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::DisplayWalletID& m)
            :RemoteCall(kk, h)
            ,m_M(m)
        {
        }

        Method::DisplayWalletID& m_M;

        void Update() override
        {
            if (!m_Phase)
            {
                hw::Proto::DisplayAddress::Out msg;
                msg.m_AddrID = m_M.m_MyIDKey;
                SendReq_T(msg);
            }

            if (1 == m_Phase)
            {
                auto pMsg = ReadReq_T<hw::Proto::DisplayAddress>();
                if (!pMsg)
                    return;

                Fin();
            }
        }

    };



#define THE_MACRO(method) \
    void RemoteKeyKeeper::InvokeAsync(Method::method& m, const Handler::Ptr& h) \
    { \
        auto pCall = std::make_shared<Impl::RemoteCall_##method>(*this, h, m); \
        pCall->Update(); \
    }

    KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

}

