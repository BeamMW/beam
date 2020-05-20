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
    using namespace hw::trezor::messages::beam;

    namespace
    {
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
            if (!v.m_Inputs.empty() && !v.m_Outputs.empty())
            {
                for (size_t i = 0; i < v.m_Inputs.size(); ++i)
                    CidCvt(v.m_Inputs[i], m.m_vInputs[i]);

                for (size_t i = 0; i < v.m_Outputs.size(); ++i)
                    CidCvt(v.m_Outputs[i], m.m_vOutputs[i]);
        
                tx2.m_pIns = &v.m_Inputs;
                tx2.m_pOuts = &v.m_Outputs;
            }
        
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
            m.m_pKernel->m_Fee = tx2.m_Krn.m_Fee;
            m.m_pKernel->m_Height.m_Min = tx2.m_Krn.m_hMin;
            m.m_pKernel->m_Height.m_Max = tx2.m_Krn.m_hMax;

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

    TrezorKeyKeeperProxy::TrezorKeyKeeperProxy(std::shared_ptr<DeviceManager> deviceManager)
        : m_DeviceManager(deviceManager)
    {
        EnsureEvtOut();
    }

    IPrivateKeyKeeper2::Status::Type TrezorKeyKeeperProxy::InvokeSync(Method::get_Kdf& m)
    {
        if (m_OwnerKdf && m.m_Root)
        {
            m.m_pPKdf = m_OwnerKdf;
            return Status::Success;
        }
        return PrivateKeyKeeper_WithMarshaller::InvokeSync(m);
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::get_Kdf& m, const Handler::Ptr& h)
    {
        m_DeviceManager->call_BeamGetPKdf(m.m_Root, m.m_iChild, true, [this, &m, h](const Message& msg, std::string session, size_t queue_size)
        {
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
                if (m.m_Root)
                    m_OwnerKdf = pubKdf; // cache owner kdf

                m.m_pPKdf = std::move(pubKdf);
                PushOut(Status::Success, h);
            }
            else
            {
                PushOut(Status::Unspecified, h);
            }
        });
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::get_NumSlots& m, const Handler::Ptr& h)
    {
        m_DeviceManager->call_BeamGetNumSlots(true, [this, &m, h](const Message& msg, std::string session, size_t queue_size)
        {
            m.m_Count = child_cast<Message, BeamNumSlots>(msg).num_slots();
            PushOut(Status::Success, h);
        });
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::CreateOutput& m, const Handler::Ptr& h)
    {
        if (m.m_hScheme < Rules::get().pForks[1].m_Height)
            return PushOut(Status::NotImplemented, h);
        
        if (!m_OwnerKdf)
            return PushOut(Status::Unspecified, h);
        
        Output::Ptr pOutp = std::make_unique<Output>();
        
        ECC::Point::Native comm;
        get_Commitment(comm, m.m_Cid); // TODO: don't block here!
        pOutp->m_Commitment = comm;
        
        // rangeproof
        ECC::Scalar::Native skDummy;
        ECC::HKdf kdfDummy;
        
        pOutp->Create(m.m_hScheme, skDummy, kdfDummy, m.m_Cid, *m_OwnerKdf, Output::OpCode::Mpc_1);
        assert(pOutp->m_pConfidential);

        BeamCrypto_CoinID cid;
        BeamCrypto_CompactPoint pt0, pt1;

        CidCvt(cid, m.m_Cid);
        pt0 = Ecc2BC(pOutp->m_pConfidential->m_Part2.m_T1);
        pt1 = Ecc2BC(pOutp->m_pConfidential->m_Part2.m_T2);
        auto copyableOutput = std::make_shared<Output::Ptr>(std::move(pOutp));
        m_DeviceManager->call_BeamGenerateRangeproof(&cid, &pt0, &pt1, nullptr, nullptr, 
            [this, &m, h, copyableOutput](const Message& msg, std::string session, size_t queue_size)
        {
            bool isSuccessful = child_cast<Message, BeamRangeproofData>(msg).is_successful();
            if (!isSuccessful)
            {
                PushOut(Status::Unspecified, h);
                return;
            }
            struct SharedResult
            {
                secp256k1_scalar m_TauX;
                BeamCrypto_CompactPoint m_Pt0;
                BeamCrypto_CompactPoint m_Pt1;
            };
            auto res = std::make_shared<SharedResult>();
            res->m_TauX = ConvertResultTo<secp256k1_scalar>(child_cast<Message, BeamRangeproofData>(msg).data_taux());
            TxExport(res->m_Pt0, child_cast<Message, BeamRangeproofData>(msg).pt0());
            TxExport(res->m_Pt1, child_cast<Message, BeamRangeproofData>(msg).pt1());

            // TODO: maybe move the rest to the caller thread (it's relatively heavy)
            Output::Ptr output = std::move(*copyableOutput);
            Ecc2BC(output->m_pConfidential->m_Part2.m_T1) = res->m_Pt0;
            Ecc2BC(output->m_pConfidential->m_Part2.m_T2) = res->m_Pt1;
                    
            ECC::Scalar::Native tauX;
            tauX.get_Raw() = res->m_TauX;
            output->m_pConfidential->m_Part3.m_TauX = tauX;
            
            ECC::Scalar::Native skDummy;
            ECC::HKdf kdfDummy;
            output->Create(m.m_hScheme, skDummy, kdfDummy, m.m_Cid, *m_OwnerKdf, Output::OpCode::Mpc_2); // Phase 3
            
            m.m_pResult.swap(output);
            
            PushOut(Status::Success, h);
        });
    }

    void TrezorKeyKeeperProxy::InvokeAsync(Method::SignReceiver& m, const Handler::Ptr& h)
    {
        BeamCrypto_TxCommon txCommon;
        Vectors v;
        TxImport(txCommon, m, v);

        BeamCrypto_TxMutualInfo txMutualInfo;
        txMutualInfo.m_MyIDKey = m.m_MyIDKey;
        txMutualInfo.m_Peer = Ecc2BC(m.m_Peer);

        m_DeviceManager->call_BeamSignTransactionReceive(txCommon, txMutualInfo, [this, &m, h, txCommon](const Message& msg, std::string session, size_t queue_size) mutable
        {
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

        m_DeviceManager->call_BeamSignTransactionSend(txCommon, txMutualInfo, txSenderParams, [this, &m, h, txCommon](const Message& msg, std::string session, size_t queue_size) mutable
        {
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

        m_DeviceManager->call_BeamSignTransactionSplit(txCommon, [this, &m, h, txCommon](const Message& msg, std::string session, size_t queue_size) mutable
        {
            TxExport(txCommon, child_cast<Message, BeamSignTransactionSplit>(msg).tx_common());
            TxExport(m, txCommon);
            PushOut(Status::Success, h);
        });
    }

}