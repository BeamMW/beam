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

namespace beam {
namespace hw {

extern "C" {
#   include "../hw_crypto/keykeeper.h"
}


#define THE_MACRO_Field(cvt, type, name) type m_##name;

#define THE_MACRO_Field_h2n(cvt, type, name) THE_MACRO_Field_h2n_##cvt(m_##name)
#define THE_MACRO_Field_h2n_0(field)
#define THE_MACRO_Field_h2n_1(field) Proto::h2n(field);

#define THE_MACRO_Field_n2h(cvt, type, name) THE_MACRO_Field_n2h_##cvt(m_##name)
#define THE_MACRO_Field_n2h_0(field)
#define THE_MACRO_Field_n2h_1(field) Proto::n2h(field);

	namespace Proto
	{
		template <typename T> void h2n_u(T& x) {
            x = ByteOrder::to_be(x);
		}

		template <typename T> void n2h_u(T& x) {
            x = ByteOrder::from_be(x);
        }

		void h2n(uint16_t& x) { h2n_u(x); }
		void n2h(uint16_t& x) { n2h_u(x); }
		void h2n(uint32_t& x) { h2n_u(x); }
		void n2h(uint32_t& x) { n2h_u(x); }
		void h2n(uint64_t& x) { h2n_u(x); }
		void n2h(uint64_t& x) { n2h_u(x); }

		void h2n(ShieldedInput& x) {
			h2n(x.m_Fee);
			h2n(x.m_TxoID.m_Amount);
			h2n(x.m_TxoID.m_AssetID);
			h2n(x.m_TxoID.m_nViewerIdx);
		}

		void h2n(CoinID& cid) {
			h2n(cid.m_Amount);
			h2n(cid.m_AssetID);
			h2n(cid.m_Idx);
			h2n(cid.m_SubIdx);
			h2n(cid.m_Type);
		}

		void h2n(TxCommonIn& tx) {
			h2n(tx.m_Ins);
			h2n(tx.m_Outs);
			h2n(tx.m_InsShielded);
			h2n(tx.m_Krn.m_Fee);
			h2n(tx.m_Krn.m_hMin);
			h2n(tx.m_Krn.m_hMax);
		}

		void h2n(TxMutualIn& mut) {
			h2n(mut.m_MyIDKey);
		}

#pragma pack (push, 1)

#define THE_MACRO(id, name) \
		struct name { \
			struct Out { \
				uint8_t m_OpCode; \
				Out() { \
					ZeroObject(*this); \
					m_OpCode = id; \
				} \
				BeamCrypto_ProtoRequest_##name(THE_MACRO_Field) \
				void h2n() { BeamCrypto_ProtoRequest_##name(THE_MACRO_Field_h2n) } \
			}; \
			struct In { \
				BeamCrypto_ProtoResponse_##name(THE_MACRO_Field) \
				void n2h() { BeamCrypto_ProtoResponse_##name(THE_MACRO_Field_n2h) } \
			}; \
			Out m_Out; \
			In m_In; \
		};

		BeamCrypto_ProtoMethods(THE_MACRO)
#undef THE_MACRO
#undef THE_MACRO_Field
#pragma pack (pop)

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


    //////////////
    // Impl
    struct RemoteKeyKeeper::Impl
    {
	    template <typename T>
	    static T& ExtendBy(ByteBuffer& buf, const T& x, size_t nExtra)
	    {
		    buf.resize(sizeof(T) + nExtra);
		    memcpy(&buf.front(), &x, sizeof(T));
		    return *reinterpret_cast<T*>(&buf.front());
	    }

        static void CidCvt(hw::CoinID&, const CoinID&);


	    struct Encoder
	    {
		    ByteBuffer m_Buf;

		    template <typename T>
		    T& ExtendByCommon(T& x, const Method::TxCommon& tx, uint32_t& nOutExtra)
		    {
			    ExtendByInternal(reinterpret_cast<const uint8_t*>(&x), static_cast<uint32_t>(sizeof(x)), x.m_Tx, tx, nOutExtra);
			    return *reinterpret_cast<T*>(&m_Buf.front());
		    }

		    void ExtendByInternal(const uint8_t* p, uint32_t nSize, hw::TxCommonIn&, const Method::TxCommon& tx, uint32_t& nOutExtra);

		    static void Import(hw::TxKernelUser&, const Method::TxCommon&);
		    static void Import(hw::TxKernelData&, const Method::TxCommon&);
		    static void Import(hw::TxCommonOut&, const Method::TxCommon&);
		    static void Import(hw::TxMutualIn&, const Method::TxMutual&);
		    static void Import(hw::ShieldedTxoID&, const ShieldedTxo::ID&);
		    static void Import(hw::ShieldedTxoUser&, const ShieldedTxo::User&);

		    static void Export(Method::TxCommon&, const hw::TxCommonOut&);
	    };

	    static Amount CalcTxBalance(Asset::ID* pAid, const Method::TxCommon&);
	    static void CalcTxBalance(Amount&, Asset::ID* pAid, Amount, Asset::ID);


        struct RemoteCall
            :public IPrivateKeyKeeper2::Handler
            ,public std::enable_shared_from_this<IPrivateKeyKeeper2::Handler>
        {
            RemoteKeyKeeper& m_This;
            Handler::Ptr m_pFinal;

            RemoteCall(RemoteKeyKeeper& kk, const Handler::Ptr& h)
                :m_This(kk)
                ,m_pFinal(h)
            {
            }

	        template <typename TOut, typename TIn>
	        void InvokeProtoEx(TOut& out, TIn& in, size_t nExOut, size_t nExIn)
	        {
		        out.h2n();

                m_This.SendRequestAsync(
                    Blob(&out, static_cast<uint32_t>(sizeof(out) + nExOut)),
                    Blob(&in, static_cast<uint32_t>(sizeof(in) + nExIn)),
                    shared_from_this());
	        }

	        template <typename T>
	        void InvokeProto(T& msg)
	        {
		        return InvokeProtoEx(msg.m_Out, msg.m_In, 0, 0);
	        }


            void Fin(Status::Type n = Status::Success) {
                m_This.PushOut(n, m_pFinal);
            }

            virtual void OnDone(Status::Type n) override
            {
                if (Status::Success == n)
                    OnRemoteData();
                else
                    Fin(n);
            }

            virtual void OnRemoteData() = 0;
        };


#define THE_MACRO(method) struct RemoteCall_##method;
        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO



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


    void RemoteKeyKeeper::Impl::Encoder::ExtendByInternal(const uint8_t* p, uint32_t nSize, hw::TxCommonIn& txIn, const Method::TxCommon& m, uint32_t& nOutExtra)
    {
	    Import(txIn.m_Krn, m); // do it before reallocation

	    txIn.m_Ins = static_cast<uint32_t>(m.m_vInputs.size());
	    txIn.m_Outs = static_cast<uint32_t>(m.m_vOutputs.size());
	    txIn.m_InsShielded = static_cast<uint32_t>(m.m_vInputsShielded.size());

	    uint32_t nExtra =
		    static_cast <uint32_t>(sizeof(hw::CoinID)) * (txIn.m_Ins + txIn.m_Outs) +
		    static_cast <uint32_t>(sizeof(hw::ShieldedInput)) * txIn.m_InsShielded;
	    nOutExtra = nExtra;

	    m_Buf.resize(nSize + nExtra);
	    memcpy(&m_Buf.front(), p, nSize);

        hw::CoinID* pCid = (hw::CoinID*) (&m_Buf.front() + nSize);

	    for (uint32_t i = 0; i < txIn.m_Ins; i++, pCid++)
	    {
		    CidCvt(*pCid, m.m_vInputs[i]);
            hw::Proto::h2n(*pCid);
	    }

	    for (uint32_t i = 0; i < txIn.m_Outs; i++, pCid++)
	    {
		    CidCvt(*pCid, m.m_vOutputs[i]);
            hw::Proto::h2n(*pCid);
	    }

        hw::ShieldedInput* pShInp = (hw::ShieldedInput*) pCid;
	    for (uint32_t i = 0; i < txIn.m_InsShielded; i++, pShInp++)
	    {
		    const auto& src = m.m_vInputsShielded[i];
		    pShInp->m_Fee = src.m_Fee;
		    Import(pShInp->m_TxoID, src);
            hw::Proto::h2n(*pShInp);
	    }
    }

    void RemoteKeyKeeper::Impl::Encoder::Import(hw::TxKernelUser& krn, const Method::TxCommon& m)
    {
	    assert(m.m_pKernel);
	    const auto& src = *m.m_pKernel;

	    krn.m_Fee = src.m_Fee;
	    krn.m_hMin = src.m_Height.m_Min;
	    krn.m_hMax = src.m_Height.m_Max;
    }

    void RemoteKeyKeeper::Impl::Encoder::Import(hw::TxKernelData& krn, const Method::TxCommon& m)
    {
	    assert(m.m_pKernel);
	    const auto& src = *m.m_pKernel;

	    krn.m_Commitment = Ecc2BC(src.m_Commitment);
	    krn.m_Signature.m_NoncePub = Ecc2BC(src.m_Signature.m_NoncePub);
	    krn.m_Signature.m_k = Ecc2BC(src.m_Signature.m_k.m_Value);
    }

    void RemoteKeyKeeper::Impl::Encoder::Import(hw::TxCommonOut& txOut, const Method::TxCommon& m)
    {
	    Import(txOut.m_Krn, m);

	    ECC::Scalar kOffs(m.m_kOffset);
	    txOut.m_kOffset = Ecc2BC(kOffs.m_Value);
    }

    void RemoteKeyKeeper::Impl::Encoder::Import(hw::TxMutualIn& txIn, const Method::TxMutual& m)
    {
	    txIn.m_MyIDKey = m.m_MyIDKey;
	    txIn.m_Peer = Ecc2BC(m.m_Peer);
    }

    void RemoteKeyKeeper::Impl::Encoder::Export(Method::TxCommon& m, const hw::TxCommonOut& txOut)
    {
	    // kernel
	    assert(m.m_pKernel);
	    auto& krn = *m.m_pKernel;

	    Ecc2BC(krn.m_Commitment) = txOut.m_Krn.m_Commitment;
	    Ecc2BC(krn.m_Signature.m_NoncePub) = txOut.m_Krn.m_Signature.m_NoncePub;
	    Ecc2BC(krn.m_Signature.m_k.m_Value) = txOut.m_Krn.m_Signature.m_k;

	    krn.UpdateID();

	    // offset
	    ECC::Scalar kOffs;
	    Ecc2BC(kOffs.m_Value) = txOut.m_kOffset;
	    m.m_kOffset = kOffs;
    }

    void RemoteKeyKeeper::Impl::Encoder::Import(hw::ShieldedTxoUser& dst, const ShieldedTxo::User& src)
    {
	    dst.m_Sender = Ecc2BC(src.m_Sender);

	    static_assert(_countof(dst.m_pMessage) == _countof(src.m_pMessage));
	    for (uint32_t i = 0; i < _countof(src.m_pMessage); i++)
		    dst.m_pMessage[i] = Ecc2BC(src.m_pMessage[i]);
    }

    void RemoteKeyKeeper::Impl::Encoder::Import(hw::ShieldedTxoID& dst, const ShieldedTxo::ID& src)
    {
	    Import(dst.m_User, src.m_User);

	    dst.m_Amount = src.m_Value;
	    dst.m_AssetID = src.m_AssetID;

	    dst.m_IsCreatedByViewer = !!src.m_Key.m_IsCreatedByViewer;
	    dst.m_nViewerIdx = src.m_Key.m_nIdx;
	    dst.m_kSerG = Ecc2BC(src.m_Key.m_kSerG.m_Value);
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
        hw::Proto::GetPKdf m_Msg;

        void Do()
        {
            if ((KdfType::Root == m_M.m_Type) && m_This.m_Cache.get_Owner(m_M.m_pPKdf))
            {
                Fin();
                return;
            }

            m_Msg.m_Out.m_Kind = (KdfType::Root == m_M.m_Type) ? 0 : 1;
            InvokeProto(m_Msg);
        }

        virtual void OnRemoteData() override
        {
            m_Msg.m_In.n2h();

            ECC::HKdfPub::Packed p;
            Ecc2BC(p.m_Secret) = m_Msg.m_In.m_Value.m_Secret;
            Ecc2BC(p.m_PkG) = m_Msg.m_In.m_Value.m_CoFactorG;
            Ecc2BC(p.m_PkJ) = m_Msg.m_In.m_Value.m_CoFactorJ;

            auto pRes = std::make_shared<ECC::HKdfPub>();
            if (Cast::Up<ECC::HKdfPub>(*pRes).Import(p))
            {
                if (KdfType::Root == m_M.m_Type)
                    m_This.m_Cache.set_Owner(pRes);

                m_M.m_pPKdf = std::move(pRes);
                Fin();
            }
            else
                Fin(Status::Unspecified);
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

        hw::Proto::Version m_Msg1;
        hw::Proto::GetNumSlots m_Msg2;
        bool m_VersionVerified = false;

        void Do()
        {
            m_M.m_Count = m_This.m_Cache.get_NumSlots();
            if (m_M.m_Count)
                Fin();
            else
                InvokeProto(m_Msg1);
        }

        virtual void OnRemoteData() override
        {
            if (m_VersionVerified)
            {
                m_Msg2.m_In.n2h();

                if (m_Msg2.m_In.m_Value)
                {
                    {
                        std::unique_lock<std::mutex> scope(m_This.m_Cache.m_Mutex);
                        m_This.m_Cache.m_Slots = m_Msg2.m_In.m_Value;
                    }

                    m_M.m_Count = m_Msg2.m_In.m_Value;
                    Fin();
                }
                else
                    Fin(c_KeyKeeper_Status_ProtoError);
            }
            else
            {
                m_Msg1.m_In.n2h();
                if (BeamCrypto_CurrentProtoVer == m_Msg1.m_In.m_Value)
                {
                    m_VersionVerified = true;
                    InvokeProto(m_Msg2);
                }
                else
                    Fin(c_KeyKeeper_Status_ProtoError);
            }
        }
    };

    struct RemoteKeyKeeper::Impl::RemoteCall_get_Commitment
        :public RemoteCall
    {
        RemoteCall_get_Commitment(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::get_Commitment& m)
            :RemoteCall(kk, h)
            ,m_M(m)
        {
        }

        Method::get_Commitment& m_M;
        hw::Proto::GetImage m_Msg;
        Method::get_Kdf m_GetKey;
        uint32_t m_Phase = 0;

        void Do()
        {

            if (m_M.m_Cid.get_ChildKdfIndex(m_Msg.m_Out.m_iChild))
            {
                m_Msg.m_Out.m_bG = 1;
                m_Msg.m_Out.m_bJ = 1;

                m_M.m_Cid.get_Hash(Cast::Reinterpret<ECC::Hash::Value>(m_Msg.m_Out.m_hvSrc));

                m_Phase = 1;
                InvokeProto(m_Msg);

            }
            else
            {
                if (m_This.m_Cache.get_Owner(m_GetKey.m_pPKdf))
                    CreateLocal();
                else
                {
                    m_GetKey.m_Type = KdfType::Root;
                    m_This.InvokeAsync(m_GetKey, shared_from_this());
                }

            }
        }

        void CreateLocal()
        {
            assert(m_GetKey.m_pPKdf);

            ECC::Point::Native comm;
            CoinID::Worker(m_M.m_Cid).Recover(comm, *m_GetKey.m_pPKdf);

            comm.Export(m_M.m_Result);
            Fin();
        }

        void CreateFromImages()
        {
            ECC::Point::Native ptG, ptJ;
            if (!ptG.ImportNnz(Cast::Reinterpret<ECC::Point>(m_Msg.m_In.m_ptImageG)) ||
                !ptJ.ImportNnz(Cast::Reinterpret<ECC::Point>(m_Msg.m_In.m_ptImageJ)))
                Fin(Status::Unspecified);

            CoinID::Worker(m_M.m_Cid).Recover(ptG, ptJ);

            ptG.Export(m_M.m_Result);
            Fin();
        }

        virtual void OnRemoteData() override
        {
            switch (m_Phase)
            {
            default:
                assert(false);
                // no break;

            case 0:
                if (!m_GetKey.m_pPKdf)
                    Fin(Status::Unspecified);
                CreateLocal();

                break;

            case 1:
                m_Msg.m_In.n2h();
                CreateFromImages();
                break;
            }
        }
    };


    struct RemoteKeyKeeper::Impl::RemoteCall_CreateOutput
        :public RemoteCall
    {
        RemoteCall_CreateOutput(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::CreateOutput& m)
            :RemoteCall(kk, h)
            ,m_M(m)
        {
        }

        Method::CreateOutput& m_M;

        Key::IPKdf::Ptr m_pOwner;
        Output::Ptr m_pOutput;

        hw::Proto::CreateOutput m_Msg;
        Method::get_Kdf m_GetKey;
        Method::get_Commitment m_GetCommitment;

        void Do()
        {
            if (m_M.m_hScheme < Rules::get().pForks[1].m_Height)
            {
                Fin(Status::NotImplemented);
                return;
            }

            if (m_This.m_Cache.get_Owner(m_pOwner))
                Create1();
            else
            {
                m_GetKey.m_Type = KdfType::Root;
                m_This.InvokeAsync(m_GetKey, shared_from_this());

            }
        }

        virtual void OnRemoteData() override
        {
            if (m_pOwner)
            {
                assert(m_pOutput);
                if (m_pOutput->m_Commitment.m_Y != 2)
                    Create3();
                else
                {
                    m_pOutput->m_Commitment = m_GetCommitment.m_Result;
                    Create2();
                }
            }
            else
            {
                assert(m_GetKey.m_pPKdf);
                m_pOwner = std::move(m_GetKey.m_pPKdf);
                Create1();
            }
        }

        void Create1()
        {
            // have owner key
            m_pOutput = std::make_unique<Output>();
            m_pOutput->m_Commitment.m_Y = 2; // not set yet

            m_GetCommitment.m_Cid = m_M.m_Cid;
            m_This.InvokeAsync(m_GetCommitment, shared_from_this());
        }

        void Create2()
        {
            // rangeproof
            CalcLocal(Output::OpCode::Mpc_1);

            assert(m_pOutput->m_pConfidential);
            auto& c = *m_pOutput->m_pConfidential;

            CidCvt(m_Msg.m_Out.m_Cid, m_M.m_Cid);
            m_Msg.m_Out.m_pT[0] = Ecc2BC(c.m_Part2.m_T1);
            m_Msg.m_Out.m_pT[1] = Ecc2BC(c.m_Part2.m_T2);

            m_Msg.m_Out.m_pKExtra[0] = Ecc2BC(m_M.m_User.m_pExtra[0].m_Value);
            m_Msg.m_Out.m_pKExtra[1] = Ecc2BC(m_M.m_User.m_pExtra[1].m_Value);

            if (m_pOutput->m_pAsset)
                m_Msg.m_Out.m_ptAssetGen = Ecc2BC(m_pOutput->m_pAsset->m_hGen);

            InvokeProto(m_Msg);
        }

        void Create3()
        {
            m_Msg.m_In.n2h();
            auto& c = *m_pOutput->m_pConfidential;

            Ecc2BC(c.m_Part2.m_T1) = m_Msg.m_In.m_pT[0];
            Ecc2BC(c.m_Part2.m_T2) = m_Msg.m_In.m_pT[1];
            Ecc2BC(c.m_Part3.m_TauX.m_Value) = m_Msg.m_In.m_TauX;

            CalcLocal(Output::OpCode::Mpc_2); // Phase 3

            m_M.m_pResult = std::move(m_pOutput);
            Fin();
        }

        void CalcLocal(Output::OpCode::Enum e)
        {
            ECC::Scalar::Native skDummy;
            ECC::HKdf kdfDummy;
            m_pOutput->Create(m_M.m_hScheme, skDummy, kdfDummy, m_M.m_Cid, *m_pOwner, e); // Phase 3
        }

    };



    struct RemoteKeyKeeper::Impl::RemoteCall_CreateInputShielded
        :public RemoteCall
    {
        Method::CreateInputShielded& m_M;
        Method::get_Kdf m_GetKey;

        uint32_t m_Phase = 0;

        hw::Proto::GetImage m_GetCommitment;
        hw::Proto::CreateShieldedInput m_Msg;
        ByteBuffer m_MsgOut;

        Lelantus::Prover m_Prover;
        ECC::Hash::Value m_hvSigmaSeed;
        ECC::Oracle m_Oracle;

        RemoteCall_CreateInputShielded(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::CreateInputShielded& m)
            :RemoteCall(kk, h)
            ,m_M(m)
            ,m_Prover(*m.m_pList, m.m_pKernel->m_SpendProof)
        {
        }

        void Do()
        {
            if (m_This.m_Cache.get_Owner(m_GetKey.m_pPKdf))
                m_Phase = 1;
            else
            {
                m_GetKey.m_Type = KdfType::Root;
                m_This.InvokeAsync(m_GetKey, shared_from_this());
            }

            static_assert(c_ShieldedInput_ChildKdf == ShieldedTxo::ID::s_iChildOut);

            m_GetCommitment.m_Out.m_bG = 1;
            m_GetCommitment.m_Out.m_bJ = 0;
            m_GetCommitment.m_Out.m_iChild = c_ShieldedInput_ChildKdf;
            m_M.get_SkOutPreimage(Cast::Reinterpret<ECC::Hash::Value>(m_GetCommitment.m_Out.m_hvSrc), m_M.m_pKernel->m_Fee);
            InvokeProto(m_GetCommitment);
        }

        bool Setup()
        {
            auto& m = m_M; // alias
            assert(m.m_pList && m.m_pKernel);
            auto& krn = *m.m_pKernel; // alias

            ShieldedTxo::Data::Params pars;
            pars.Set(*m_GetKey.m_pPKdf, m);
            ShieldedTxo::Data::Params::Plus plus(pars);

            Encoder::Import(m_Msg.m_Out.m_Inp.m_TxoID, m);

            m_Msg.m_Out.m_hMin = krn.m_Height.m_Min;
            m_Msg.m_Out.m_hMax = krn.m_Height.m_Max;
            m_Msg.m_Out.m_WindowEnd = krn.m_WindowEnd;
            m_Msg.m_Out.m_Sigma_M = krn.m_SpendProof.m_Cfg.M;
            m_Msg.m_Out.m_Sigma_n = krn.m_SpendProof.m_Cfg.n;
            m_Msg.m_Out.m_Inp.m_Fee = krn.m_Fee;

            ECC::Scalar sk_;
            sk_ = plus.m_skFull;
            m_Msg.m_Out.m_OutpSk = Ecc2BC(sk_.m_Value);

            Lelantus::Proof& proof = krn.m_SpendProof;

            m_Prover.m_Witness.m_V = 0; // not used
            m_Prover.m_Witness.m_L = m.m_iIdx;

            // output commitment
            ECC::Point::Native comm;
            if (!comm.ImportNnz(Cast::Reinterpret<ECC::Point>(m_GetCommitment.m_In.m_ptImageG)))
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

                ECC::Scalar::Native skBlind;
                m_GetKey.m_pPKdf->DerivePKey(skBlind, m_hvSigmaSeed);

                krn.m_pAsset = std::make_unique<Asset::Proof>();
                krn.m_pAsset->Create(plus.m_hGen, skBlind, m.m_AssetID, plus.m_hGen);


                sk_ = -skBlind;
                m_Msg.m_Out.m_AssetSk = Ecc2BC(sk_.m_Value);
            }
            else
                ZeroObject(m_Msg.m_Out.m_AssetSk);

            krn.UpdateMsg();
            m_Oracle << krn.m_Msg;

            if (krn.m_Height.m_Min >= Rules::get().pForks[3].m_Height)
            {
                m_Oracle << krn.m_NotSerialized.m_hvShieldedState;
                Asset::Proof::Expose(m_Oracle, krn.m_Height.m_Min, krn.m_pAsset);

                m_Msg.m_Out.m_ShieldedState = Ecc2BC(krn.m_NotSerialized.m_hvShieldedState);

                if (krn.m_pAsset)
                    m_Msg.m_Out.m_ptAssetGen = Ecc2BC(krn.m_pAsset->m_hGen);
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

        void Create1()
        {
            assert(m_GetKey.m_pPKdf);

            if (!Setup())
                return;

            {
                ExecutorMT_R exec;
                Executor::Scope scope(exec);

                // proof phase1 generation (the most computationally expensive)
                m_Prover.Generate(m_hvSigmaSeed, m_Oracle, nullptr, Lelantus::Prover::Phase::Step1);
            }

            auto& proof = m_Prover.m_Sigma.m_Proof;

            // Invoke HW device
            m_Msg.m_Out.m_pABCD[0] = Ecc2BC(proof.m_Part1.m_A);
            m_Msg.m_Out.m_pABCD[1] = Ecc2BC(proof.m_Part1.m_B);
            m_Msg.m_Out.m_pABCD[2] = Ecc2BC(proof.m_Part1.m_C);
            m_Msg.m_Out.m_pABCD[3] = Ecc2BC(proof.m_Part1.m_D);

            const auto& vec = m_Prover.m_Sigma.m_Proof.m_Part1.m_vG;
            size_t nSize = sizeof(vec[0]) * vec.size();

            auto& msgOut = ExtendBy(m_MsgOut, m_Msg.m_Out, nSize);
            if (nSize)
                memcpy(&msgOut + 1, reinterpret_cast<const uint8_t*>(&vec.front()), nSize);

            InvokeProtoEx(msgOut, m_Msg.m_In, nSize, 0);
        }

        void Create2()
        {
            m_Msg.m_In.n2h();

            auto& proof = Cast::Up<Lelantus::Proof>(m_Prover.m_Sigma.m_Proof);

            // import SigGen and vG[0]
            Ecc2BC(proof.m_Part1.m_vG.front()) = m_Msg.m_In.m_G0;
            Ecc2BC(proof.m_Part2.m_zR.m_Value) = m_Msg.m_In.m_zR;

            Ecc2BC(proof.m_Signature.m_NoncePub) = m_Msg.m_In.m_NoncePub;
            Ecc2BC(proof.m_Signature.m_pK[0].m_Value) = m_Msg.m_In.m_pSig[0];
            Ecc2BC(proof.m_Signature.m_pK[1].m_Value) = m_Msg.m_In.m_pSig[1];

            // phase2
            m_Prover.Generate(m_hvSigmaSeed, m_Oracle, nullptr, Lelantus::Prover::Phase::Step2);

            // finished
            m_M.m_pKernel->MsgToID();
            Fin();
        }

        virtual void OnRemoteData() override
        {
            switch (m_Phase)
            {
            case 0:
                if (!m_GetKey.m_pPKdf)
                    Fin(Status::Unspecified);

                m_Phase = 1;
                break;

            case 1:
                Create1();
                m_Phase = 2;
                break;

            case 2:
                Create2();
            }

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

        hw::Proto::CreateShieldedVouchers::Out m_MsgOut;
        ByteBuffer m_MsgIn;
        uint32_t m_MaxCount;

        void Do()
        {
            if (!m_M.m_Count)
            {
                Fin();
                return;
            }

            m_MaxCount = std::min(m_M.m_Count, 30U);
            size_t nSize = sizeof(hw::ShieldedVoucher) * m_MaxCount;

            m_MsgOut.m_Count = m_MaxCount;
            m_MsgOut.m_Nonce0 = Ecc2BC(m_M.m_Nonce);
            m_MsgOut.m_nMyIDKey = m_M.m_MyIDKey;

            hw::Proto::CreateShieldedVouchers::In dummy;
            auto& msgIn = ExtendBy(m_MsgIn, dummy, nSize);

            InvokeProtoEx(m_MsgOut, msgIn, 0, nSize);
        }

        virtual void OnRemoteData() override
        {
            auto& msgIn = *reinterpret_cast<hw::Proto::CreateShieldedVouchers::In*>(&m_MsgIn.front());
            msgIn.n2h();

            if (!msgIn.m_Count || (msgIn.m_Count > m_MaxCount))
            {
                Fin(c_KeyKeeper_Status_ProtoError);
                return;
            }

            auto* pRes = reinterpret_cast<hw::ShieldedVoucher*>(&msgIn + 1);

            m_M.m_Res.resize(msgIn.m_Count);
            for (uint32_t i = 0; i < msgIn.m_Count; i++)
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
    };


    struct RemoteKeyKeeper::Impl::RemoteCall_SignReceiver
        :public RemoteCall
    {
        RemoteCall_SignReceiver(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::SignReceiver& m)
            :RemoteCall(kk, h)
            ,m_M(m)
        {
        }

        Method::SignReceiver& m_M;
        hw::Proto::TxReceive m_Msg;
        Impl::Encoder m_Enc;

        void Do()
        {
            uint32_t nOutExtra;

            auto& out = m_Enc.ExtendByCommon(m_Msg.m_Out, m_M, nOutExtra);
            m_Enc.Import(out.m_Krn, m_M);
            m_Enc.Import(out.m_Mut, m_M);

            InvokeProtoEx(out, m_Msg.m_In, nOutExtra, 0);
        }

        virtual void OnRemoteData() override
        {
            m_Msg.m_In.n2h();
            m_Enc.Export(m_M, m_Msg.m_In.m_Tx);

            Ecc2BC(m_M.m_PaymentProofSignature.m_NoncePub) = m_Msg.m_In.m_PaymentProof.m_NoncePub;
            Ecc2BC(m_M.m_PaymentProofSignature.m_k.m_Value) = m_Msg.m_In.m_PaymentProof.m_k;
            Fin();
        }
    };

    struct RemoteKeyKeeper::Impl::RemoteCall_SignSender
        :public RemoteCall
    {
        RemoteCall_SignSender(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::SignSender& m)
            :RemoteCall(kk, h)
            ,m_M(m)
        {
        }

        Method::SignSender& m_M;
        hw::Proto::TxSend1 m_Msg1; // TODO union
        hw::Proto::TxSend2 m_Msg2;
        Impl::Encoder m_Enc;
        bool m_Initial;

        void Do()
        {
            m_Initial = (m_M.m_UserAgreement == Zero);
            uint32_t nOutExtra;

            if (m_Initial)
            {
                auto& out = m_Enc.ExtendByCommon(m_Msg1.m_Out, m_M, nOutExtra);

                out.m_iSlot = m_M.m_Slot;
                m_Enc.Import(out.m_Mut, m_M);

                InvokeProtoEx(out, m_Msg1.m_In, nOutExtra, 0);
            }
            else
            {
                auto& out = m_Enc.ExtendByCommon(m_Msg2.m_Out, m_M, nOutExtra);

                out.m_iSlot = m_M.m_Slot;
                m_Enc.Import(out.m_Mut, m_M);

                out.m_PaymentProof.m_NoncePub = Ecc2BC(m_M.m_PaymentProofSignature.m_NoncePub);
                out.m_PaymentProof.m_k = Ecc2BC(m_M.m_PaymentProofSignature.m_k.m_Value);

                auto& krn = *m_M.m_pKernel;

                out.m_UserAgreement = Ecc2BC(m_M.m_UserAgreement);
                out.m_HalfKrn.m_Commitment = Ecc2BC(krn.m_Commitment);
                out.m_HalfKrn.m_NoncePub = Ecc2BC(krn.m_Signature.m_NoncePub);

                InvokeProtoEx(out, m_Msg2.m_In, nOutExtra, 0);
            }
        }

        virtual void OnRemoteData() override
        {
            auto& krn = *m_M.m_pKernel;

            if (m_Initial)
            {
                m_Msg1.m_In.n2h();

                Ecc2BC(m_M.m_UserAgreement) = m_Msg1.m_In.m_UserAgreement;
                Ecc2BC(krn.m_Commitment) = m_Msg1.m_In.m_HalfKrn.m_Commitment;
                Ecc2BC(krn.m_Signature.m_NoncePub) = m_Msg1.m_In.m_HalfKrn.m_NoncePub;

                krn.UpdateID(); // not really required, the kernel isn't full yet
            }
            else
            {
                m_Msg2.m_In.n2h();
                // add scalars
                ECC::Scalar k_;
                Ecc2BC(k_.m_Value) = m_Msg2.m_In.m_kSig;

                ECC::Scalar::Native k(krn.m_Signature.m_k);
                k += k_;
                krn.m_Signature.m_k = k;

                Ecc2BC(k_.m_Value) = m_Msg2.m_In.m_kOffset;
                m_M.m_kOffset += k_;
            }

            Fin();
        }
    };

    struct RemoteKeyKeeper::Impl::RemoteCall_SignSendShielded
        :public RemoteCall
    {
        RemoteCall_SignSendShielded(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::SignSendShielded& m)
            :RemoteCall(kk, h)
            ,m_M(m)
        {
        }

        Method::SignSendShielded& m_M;
        hw::Proto::TxSendShielded m_Msg;
        Encoder m_Enc;
        TxKernelShieldedOutput::Ptr m_pOutp;

        void Do()
        {
            uint32_t nOutExtra;

            auto& out = m_Enc.ExtendByCommon(m_Msg.m_Out, m_M, nOutExtra);
            out.m_Mut.m_Peer = Ecc2BC(m_M.m_Peer);
            out.m_Mut.m_MyIDKey = m_M.m_MyIDKey;
            out.m_HideAssetAlways = m_M.m_HideAssetAlways;
            m_Enc.Import(out.m_User, m_M.m_User);

            out.m_Voucher.m_SerialPub = Ecc2BC(m_M.m_Voucher.m_Ticket.m_SerialPub);
            out.m_Voucher.m_NoncePub = Ecc2BC(m_M.m_Voucher.m_Ticket.m_Signature.m_NoncePub);
            out.m_Voucher.m_pK[0] = Ecc2BC(m_M.m_Voucher.m_Ticket.m_Signature.m_pK[0].m_Value);
            out.m_Voucher.m_pK[1] = Ecc2BC(m_M.m_Voucher.m_Ticket.m_Signature.m_pK[1].m_Value);
            out.m_Voucher.m_SharedSecret = Ecc2BC(m_M.m_Voucher.m_SharedSecret);
            out.m_Voucher.m_Signature.m_NoncePub = Ecc2BC(m_M.m_Voucher.m_Signature.m_NoncePub);
            out.m_Voucher.m_Signature.m_k = Ecc2BC(m_M.m_Voucher.m_Signature.m_k.m_Value);

            ShieldedTxo::Data::OutputParams op;

            // the amount and asset are not directly specified, they must be deduced from tx balance.
            // Don't care about value overflow, or asset ambiguity, this will be re-checked by the HW wallet anyway.

            op.m_Value = CalcTxBalance(nullptr, m_M);
            op.m_Value -= out.m_Tx.m_Krn.m_Fee;

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
                out.m_ptAssetGen = Ecc2BC(krn1.m_Txo.m_pAsset->m_hGen);

            SerializerIntoStaticBuf ser(&out.m_RangeProof);
            ser& krn1.m_Txo.m_RangeProof;
            assert(ser.get_Size(&out.m_RangeProof) == sizeof(out.m_RangeProof));

            InvokeProtoEx(out, m_Msg.m_In, nOutExtra, 0);
        }

        virtual void OnRemoteData() override
        {
            m_Msg.m_In.n2h();
            m_M.m_pKernel->m_vNested.push_back(std::move(m_pOutp));
            m_Enc.Export(m_M, m_Msg.m_In.m_Tx);

            Fin();
        }
    };

    struct RemoteKeyKeeper::Impl::RemoteCall_SignSplit
        :public RemoteCall
    {
        RemoteCall_SignSplit(RemoteKeyKeeper& kk, const Handler::Ptr& h, Method::SignSplit& m)
            :RemoteCall(kk, h)
            ,m_M(m)
        {
        }

        Method::SignSplit& m_M;
        hw::Proto::TxSplit m_Msg;
        Encoder m_Enc;

        void Do()
        {
            uint32_t nOutExtra;
            auto& out = m_Enc.ExtendByCommon(m_Msg.m_Out, m_M, nOutExtra);
            InvokeProtoEx(out, m_Msg.m_In, nOutExtra, 0);
        }

        virtual void OnRemoteData() override
        {
            m_Msg.m_In.n2h();
            m_Enc.Export(m_M, m_Msg.m_In.m_Tx);
            Fin();
        }
    };



#define THE_MACRO(method) \
    void RemoteKeyKeeper::InvokeAsync(Method::method& m, const Handler::Ptr& h) \
    { \
        auto pCall = std::make_shared<Impl::RemoteCall_##method>(*this, h, m); \
        pCall->Do(); \
    }

    KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

}

