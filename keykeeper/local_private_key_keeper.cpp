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

#include "local_private_key_keeper.h"
#include "core/shielded.h"
#include "utility/logger.h"
#include "utility/executor.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    /////////////////////////
    // LocalPrivateKeyKeeper2
    LocalPrivateKeyKeeper2::LocalPrivateKeyKeeper2(const Key::IKdf::Ptr& pKdf)
        :m_pKdf(pKdf)
    {
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::ToImage(Point::Native& res, uint32_t iGen, const Scalar::Native& sk)
    {
        const Generator::Obscured* pGen;

        switch (iGen)
        {
        case 0: pGen = &Context::get().G; break;
        case 1: pGen = &Context::get().J; break;
        case 2: pGen = &Context::get().H_Big; break;
        default:
            return Status::Unspecified;
        }

        res = (*pGen) * sk;
        return Status::Success;
    }

    struct LocalPrivateKeyKeeper2::Aggregation
    {
        LocalPrivateKeyKeeper2& m_This;
        Aggregation(LocalPrivateKeyKeeper2& v) :m_This(v) {}

        ECC::Scalar::Native m_sk;
        Asset::ID m_AssetID;
        bool m_NonConventional;

        static bool Add(Amount& trg, Amount val)
        {
            trg += val;
            return (trg >= val); // test for overflow
        }

        static bool Sub(Amount& trg, Amount val)
        {
            if (trg < val)
                return false;
            trg -= val;
            return true;
        }

        struct Values
        {
            Amount m_Beam;
            Amount m_Asset;

            bool Subtract(const Values& v)
            {
                return
                    Sub(m_Beam, v.m_Beam) &&
                    Sub(m_Asset, v.m_Asset);
            }
        };

        Values m_Ins;
        Values m_Outs;
        Amount m_TotalFee = Amount(0);

        bool Add(Values& vals, Amount v, Asset::ID aid)
        {
            bool bAdded = false;
            if (aid)
            {
                if (m_AssetID)
                {
                    if (m_AssetID != aid)
                        return false; // mixed assets are not allowed in a single tx
                }
                else
                    m_AssetID = aid;

                bAdded = Add(vals.m_Asset, v);
            }
            else
                bAdded = Add(vals.m_Beam, v);

            return bAdded;
        }

        bool Aggregate(const Method::TxCommon&);
        bool Aggregate(const std::vector<CoinID>&, bool bOuts);
        bool Aggregate(const std::vector<ShieldedInput>&);

        bool ValidateSend();
    };

    bool LocalPrivateKeyKeeper2::Aggregation::Aggregate(const Method::TxCommon& tx)
    {
        if (!tx.m_pKernel)
            return false;
        TxKernelStd& krn = *tx.m_pKernel;
        m_TotalFee = krn.m_Fee;

        m_NonConventional = tx.m_NonConventional;
        if (m_NonConventional)
        {
            if (m_This.IsTrustless())
                return false;
        }
        else
        {
            if (krn.m_CanEmbed ||
                krn.m_pHashLock ||
                krn.m_pRelativeLock ||
                !krn.m_vNested.empty())
                return false; // non-trivial kernels should not be supported

            if ((krn.m_Height.m_Min < Rules::get().pForks[1].m_Height) && m_This.IsTrustless())
                return false; // disallow weak scheme
        }

        m_AssetID = 0;
        ZeroObject(m_Ins);
        ZeroObject(m_Outs);

        if (!Aggregate(tx.m_vInputs, false))
            return false;

        if (!Aggregate(tx.m_vInputsShielded))
            return false;

        m_sk = -m_sk;

        if (!Aggregate(tx.m_vOutputs, true))
            return false;

        return true;
    }

    bool LocalPrivateKeyKeeper2::Aggregation::Aggregate(const std::vector<CoinID>& v, bool bOuts)
    {
        Values& vals = bOuts ? m_Outs : m_Ins;

        Scalar::Native sk;
        for (size_t i = 0; i < v.size(); i++)
        {
            const CoinID& cid = v[i];

            CoinID::Worker(cid).Create(sk, *cid.get_ChildKdf(m_This.m_pKdf));
            m_sk += sk;

            if (m_NonConventional)
                continue; // ignore values

            switch (cid.get_Scheme())
            {
            case CoinID::Scheme::V0:
            case CoinID::Scheme::V1:
            case CoinID::Scheme::BB21:
                // disallow weak scheme
                if (bOuts)
                    return false; // no reason to create new weak outputs

                if (m_This.IsTrustless())
                    return false;
            }

            if (bOuts && m_This.IsTrustless())
            {
                Key::Index iSubKey;
                if (cid.get_ChildKdfIndex(iSubKey) && iSubKey)
                    return false; // trustless wallet should not send funds to child subkeys (potentially belonging to miners)
            }

            if (!Add(vals, cid.m_Value,  cid.m_AssetID))
                return false; // overflow
        }

        return true;
    }

    bool LocalPrivateKeyKeeper2::Aggregation::Aggregate(const std::vector<ShieldedInput>& v)
    {
        Values& vals = m_Ins;
        Scalar::Native sk;
        for (size_t i = 0; i < v.size(); i++)
        {
            const ShieldedInput& si = v[i];
            si.get_SkOut(sk, si.m_Fee, *m_This.m_pKdf);

            m_sk += sk;

            if (m_NonConventional)
                continue; // ignore values

            if (!Add(vals, si.m_Value, si.m_AssetID))
                return false; // overflow

            if (!Add(m_TotalFee, si.m_Fee))
                return false; // overflow
        }

        return true;
    }

    bool LocalPrivateKeyKeeper2::Aggregation::ValidateSend()
    {
        if (!m_Ins.Subtract(m_Outs))
            return false; // not sending

        if (m_AssetID)
        {
            if (!m_Ins.m_Asset)
                return false; // asset involved, but no net transfer

            if (m_Ins.m_Beam != m_TotalFee)
                return false; // all beams must be consumed by the fee
        }
        else
        {
            if (m_Ins.m_Beam <= m_TotalFee)
                return false; // not sending

            m_Ins.m_Asset = m_Ins.m_Beam - m_TotalFee;
        }

        return true;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::get_Kdf& x)
    {
        Key::IKdf::Ptr pKdf;

        switch (x.m_Type)
        {
        case KdfType::Root:
            pKdf = m_pKdf;
            break;

        case KdfType::Sbbs:
            pKdf = MasterKey::get_Child(*m_pKdf, Key::Index(-1)); // definitely won't be used for any coin
            break;

        default:
            return Status::Unspecified;
        }
        
        if (!IsTrustless())
            x.m_pKdf = pKdf;

        x.m_pPKdf = std::move(pKdf);

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::get_NumSlots& x)
    {
        x.m_Count = get_NumSlots();
        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::get_Commitment& x)
    {
        ECC::Point::Native pt;
        CoinID::Worker(x.m_Cid).Recover(pt, *x.m_Cid.get_ChildKdf(m_pKdf));

        x.m_Result = pt;
        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::CreateOutput& x)
    {
        if (IsTrustless())
        {
            // disallow weak paramters
            if (x.m_hScheme < Rules::get().pForks[1].m_Height)
                return Status::Unspecified; // blinding factor can be tampered without user permission
        }

        x.m_pResult.reset(new Output);

        Scalar::Native sk;
        x.m_pResult->Create(x.m_hScheme, sk, *x.m_Cid.get_ChildKdf(m_pKdf), x.m_Cid, *m_pKdf, Output::OpCode::Standard, &x.m_User);

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::CreateInputShielded& x)
    {
        assert(x.m_pKernel && x.m_pList);

        Lelantus::Prover prover(*x.m_pList, x.m_pKernel->m_SpendProof);

        ShieldedTxo::DataParams sdp;
        sdp.Set(*m_pKdf, x);

        Key::IKdf::Ptr pSerialPrivate;
        ShieldedTxo::Viewer::GenerateSerPrivate(pSerialPrivate, *m_pKdf, x.m_Key.m_nIdx);
        pSerialPrivate->DeriveKey(prover.m_Witness.m_SpendSk, sdp.m_Ticket.m_SerialPreimage);

        prover.m_Witness.m_L = x.m_iIdx;
        prover.m_Witness.m_R = sdp.m_Ticket.m_pK[0];
        prover.m_Witness.m_R += sdp.m_Output.m_k;
        prover.m_Witness.m_V = sdp.m_Output.m_Value;

        x.m_pKernel->UpdateMsg();
        x.get_SkOut(prover.m_Witness.m_R_Output, x.m_pKernel->m_Fee, *m_pKdf);

        ExecutorMT_R exec;
        Executor::Scope scope(exec);
        x.m_pKernel->Sign(prover, x.m_AssetID);

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::CreateVoucherShielded& x)
    {
        if (!x.m_Count)
            return Status::Success;
        std::setmin(x.m_Count, 30U);

        ECC::Scalar::Native sk;
        m_pKdf->DeriveKey(sk, Key::ID(x.m_iEndpoint, Key::Type::EndPoint));
        PeerID pid;
        pid.FromSk(sk);

        ShieldedTxo::Viewer viewer;
        viewer.FromOwner(*m_pKdf, 0);

        x.m_Res.reserve(x.m_Count);

        for (uint32_t i = 0; ; )
        {
            ShieldedTxo::Voucher& res = x.m_Res.emplace_back();

            ShieldedTxo::Data::TicketParams tp;
            tp.Generate(res.m_Ticket, viewer, x.m_Nonce);

            res.m_SharedSecret = tp.m_SharedSecret;

            ECC::Hash::Value hvMsg;
            res.get_Hash(hvMsg);
            res.m_Signature.Sign(hvMsg, sk);

            if (++i == x.m_Count)
                break;

            ECC::Hash::Processor()
                << "sh.v.n"
                << x.m_Nonce
                >> x.m_Nonce;
        }

        return Status::Success;
    }

    void LocalPrivateKeyKeeper2::UpdateOffset(Method::TxCommon& tx, const Scalar::Native& kDiff, const Scalar::Native& kKrn)
    {
        Scalar::Native k = kDiff + kKrn;
        k = -k;
        tx.m_kOffset += k;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::SignReceiver& x)
    {
        Aggregation aggr(*this);
        if (!aggr.Aggregate(x))
            return Status::Unspecified;

        Aggregation::Values& vals = aggr.m_Outs; // alias

        if (!x.m_NonConventional)
        {
            if (!vals.Subtract(aggr.m_Ins))
                return Status::Unspecified; // not receiving

            if (vals.m_Beam)
            {
                if (aggr.m_AssetID)
                    return Status::Unspecified; // can't receive both

                vals.m_Asset = vals.m_Beam;
            }
            else
            {
                if (!vals.m_Asset)
                    return Status::Unspecified; // should receive at least something
            }
        }

        assert(x.m_pKernel);
        TxKernelStd& krn = *x.m_pKernel;

        Scalar::Native kKrn, kNonce;

        // Hash *ALL* the parameters, make the context unique
        krn.UpdateID();
        Hash::Value& hv = krn.m_Internal.m_ID; // alias

        Hash::Processor()
            << krn.m_Internal.m_ID
            << krn.m_Signature.m_NoncePub
            << x.m_NonConventional
            << x.m_Peer
            << x.m_iEndpoint
            << aggr.m_sk
            << vals.m_Asset
            << aggr.m_AssetID
            >> hv;

        NonceGenerator ng("hw-wlt-rcv");
        ng << hv;
        ng >> kKrn;
        ng >> kNonce;

        Point::Native commitment;
        if (!commitment.Import(krn.m_Commitment))
            return Status::Unspecified;

        Point::Native temp;
        temp = Context::get().G * kKrn; // public kernel commitment
        commitment += temp;
        krn.m_Commitment = commitment;

        if (!commitment.Import(krn.m_Signature.m_NoncePub))
            return Status::Unspecified;

        temp = Context::get().G * kNonce;
        commitment += temp;
        krn.m_Signature.m_NoncePub = commitment;

        krn.UpdateID();

        krn.m_Signature.SignPartial(hv, kKrn, kNonce);
        UpdateOffset(x, aggr.m_sk, kKrn);

        if (x.m_iEndpoint && !x.m_NonConventional)
        {
            PaymentConfirmation pc;
            pc.m_KernelID = hv;
            pc.m_Sender = x.m_Peer;
            pc.m_Value = vals.m_Asset;
            pc.m_AssetID = aggr.m_AssetID;

            m_pKdf->DeriveKey(kKrn, Key::ID(x.m_iEndpoint, Key::Type::EndPoint));

            PeerID wid;
            wid.FromSk(kKrn);
            pc.Sign(kKrn);

            x.m_PaymentProofSignature = pc.m_Signature;
        }

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::SignSender& x)
    {
        Aggregation aggr(*this);
        if (!aggr.Aggregate(x))
            return Status::Unspecified;

        assert(x.m_pKernel);
        TxKernelStd& krn = *x.m_pKernel;

        Aggregation::Values& vals = aggr.m_Ins; // alias

        if (!x.m_NonConventional)
        {
            if (x.m_Peer == Zero)
                return Status::UserAbort; // conventional transfers must always be signed

            if (!aggr.ValidateSend())
                return Status::Unspecified; // not sending
        }

        if (x.m_Slot >= get_NumSlots())
            return Status::Unspecified;

        Scalar::Native kNonce;

        if (x.m_iEndpoint)
        {
            m_pKdf->DeriveKey(kNonce, Key::ID(x.m_iEndpoint, Key::Type::EndPoint));
            x.m_MyID.FromSk(kNonce);
        }
        else
        {
            // legacy. We need to verify the payment proof vs externally-specified our ID (usually SBBS address)
            if (IsTrustless())
                return Status::Unspecified;
        }

        get_Nonce(kNonce, x.m_Slot);

        // during negotiation kernel height and commitment are adjusted. We should only commit to the Fee
        Hash::Value& hv = krn.m_Internal.m_ID; // alias. Just reuse this variable
        Hash::Processor()
            << krn.m_Fee
            << x.m_Peer
            << x.m_MyID
            << x.m_NonConventional
            << aggr.m_sk
            << vals.m_Asset
            << aggr.m_AssetID
            << kNonce
            >> hv;

        Scalar::Native kKrn;

        NonceGenerator ng("hw-wlt-snd");
        ng << hv;
        ng >> kKrn;

        Hash::Processor()
            << "tx.token"
            << kKrn
            >> hv;

        if (hv == Zero)
            hv = 1U;

        if (x.m_UserAgreement == Zero)
        {
            if (IsTrustless())
            {
                Status::Type res = ConfirmSpend(vals.m_Asset, aggr.m_AssetID, x.m_Peer, krn, aggr.m_TotalFee, false);
                if (Status::Success != res)
                    return res;
            }

            x.m_UserAgreement = hv;

            Point::Native commitment = Context::get().G * kKrn;
            krn.m_Commitment = commitment;

            commitment = Context::get().G * kNonce;
            krn.m_Signature.m_NoncePub = commitment;

            return Status::Success;
        }

        if (x.m_UserAgreement != hv)
            return Status::Unspecified; // incorrect user agreement token

        krn.UpdateID();

        if ((x.m_Peer != Zero) && !x.m_NonConventional)
        {
            PaymentConfirmation pc;
            pc.m_KernelID = krn.m_Internal.m_ID;
            pc.m_Value = vals.m_Asset;
            pc.m_AssetID = aggr.m_AssetID;
            pc.m_Sender = x.m_MyID;
            pc.m_Signature = x.m_PaymentProofSignature;
            if (!pc.IsValid(x.m_Peer))
            {
                LOG_DEBUG() << "Payment proof confimation failed";
                return Status::Unspecified;
            }
        }

        if (IsTrustless())
        {
            // 2nd user confirmation request. Now the kernel is complete, its ID can be calculated
            Status::Type res = ConfirmSpend(vals.m_Asset, aggr.m_AssetID, x.m_Peer, krn, aggr.m_TotalFee, true);
            if (Status::Success != res)
                return res;
        }

        Regenerate(x.m_Slot);

        Scalar::Native kSig = krn.m_Signature.m_k;
        krn.m_Signature.SignPartial(hv, kKrn, kNonce);
        kSig += krn.m_Signature.m_k;
        krn.m_Signature.m_k = kSig;

        UpdateOffset(x, aggr.m_sk, kKrn);

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::SignSendShielded& x)
    {
        Aggregation aggr(*this);
        if (!aggr.Aggregate(x))
            return Status::Unspecified;

        assert(x.m_pKernel);
        TxKernelStd& krn = *x.m_pKernel;

        Aggregation::Values& vals = aggr.m_Ins; // alias

        ECC::Scalar::Native kKrn, kNonce;

        if (!x.m_NonConventional)
        {
            if (!aggr.ValidateSend())
                return Status::Unspecified; // not sending

            if (!x.m_Voucher.IsValid(x.m_Peer)) // will fail if m_Peer is Zero
                return Status::Unspecified;

            if (x.m_iEndpoint)
            {
                m_pKdf->DeriveKey(kKrn, Key::ID(x.m_iEndpoint, Key::Type::EndPoint));

                PeerID pid;
                pid.FromSk(kKrn);
                if (pid != x.m_Peer)
                    return Status::Unspecified;
            }
        }

        ShieldedTxo::Data::Params pars;
        pars.m_Output.m_Value = vals.m_Asset;
        pars.m_Output.m_AssetID = aggr.m_AssetID;
        pars.m_Output.m_User = x.m_User;
        pars.m_Output.Restore_kG(x.m_Voucher.m_SharedSecret);

        TxKernelShieldedOutput::Ptr pOutp = std::make_unique<TxKernelShieldedOutput>();
        TxKernelShieldedOutput& krn1 = *pOutp;

        krn1.m_CanEmbed = true;
        krn1.m_Txo.m_Ticket = x.m_Voucher.m_Ticket;

        krn1.UpdateMsg();
        ECC::Oracle oracle;
        oracle << krn1.m_Msg;

        pars.m_Output.Generate(krn1.m_Txo, x.m_Voucher.m_SharedSecret, krn.m_Height.m_Min, oracle, x.m_HideAssetAlways);
        krn1.MsgToID();

        assert(krn.m_vNested.empty());
        krn.m_vNested.push_back(std::move(pOutp));

        // select blinding factor for the outer kernel.
        Hash::Processor()
            << krn1.m_Internal.m_ID
            << krn.m_Height.m_Min
            << krn.m_Height.m_Max
            << krn.m_Fee
            << aggr.m_sk
            >> krn.m_Internal.m_ID;

        NonceGenerator ng("hw-wlt-snd-sh");
        ng
            << krn.m_Internal.m_ID
            >> kKrn;
        ng >> kNonce;

        krn.m_Commitment = ECC::Context::get().G * kKrn;
        krn.m_Signature.m_NoncePub = ECC::Context::get().G * kNonce;
        krn.UpdateID();

        if (IsTrustless())
        {
            Status::Type res = x.m_iEndpoint ?
                ConfirmSpend(0, 0, Zero, krn, aggr.m_TotalFee, true) : // sending to self, it's a split in fact
                ConfirmSpend(vals.m_Asset, aggr.m_AssetID, x.m_Peer, krn, aggr.m_TotalFee, true);

            if (Status::Success != res)
                return res;
        }

        krn.m_Signature.SignPartial(krn.m_Internal.m_ID, kKrn, kNonce);

        kKrn += pars.m_Output.m_k;
        UpdateOffset(x, aggr.m_sk, kKrn);

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::SignSplit& x)
    {
        Aggregation aggr(*this);
        if (!aggr.Aggregate(x))
            return Status::Unspecified;

        Aggregation::Values& vals = aggr.m_Ins; // alias
        if (!vals.Subtract(aggr.m_Outs))
            return Status::Unspecified; // not spending

        assert(x.m_pKernel);
        TxKernelStd& krn = *x.m_pKernel;

        if (vals.m_Asset || vals.m_Beam != aggr.m_TotalFee)
            return Status::Unspecified; // some funds are missing!

        Scalar::Native kKrn, kNonce;

        Hash::Value& hv = krn.m_Internal.m_ID; // alias

        Hash::Processor()
            << krn.m_Height.m_Min
            << krn.m_Height.m_Max
            << krn.m_Fee
            << aggr.m_sk
            >> hv;

        NonceGenerator ng("hw-wlt-split");
        ng << hv;
        ng >> kKrn;
        ng >> kNonce;

        Point::Native commitment = Context::get().G * kKrn; // public kernel commitment
        krn.m_Commitment = commitment;

        commitment = Context::get().G * kNonce;
        krn.m_Signature.m_NoncePub = commitment;

        krn.UpdateID();

        Status::Type res = ConfirmSpend(0, 0, Zero, krn, aggr.m_TotalFee, true);
        if (Status::Success != res)
            return res;

        krn.m_Signature.SignPartial(hv, kKrn, kNonce);
        UpdateOffset(x, aggr.m_sk, kKrn);

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::DisplayEndpoint& x)
    {
        return Status::Success;
    }

    /////////////////////////
    // LocalPrivateKeyKeeperStd
    LocalPrivateKeyKeeperStd::LocalPrivateKeyKeeperStd(const ECC::Key::IKdf::Ptr& pkdf, const Slot::Type numSlots)
        : LocalPrivateKeyKeeper2(pkdf)
        , m_numSlots(numSlots)
    {
    }

    IPrivateKeyKeeper2::Slot::Type LocalPrivateKeyKeeperStd::get_NumSlots()
    {
        return m_numSlots;
    }

    ECC::Hash::Value* LocalPrivateKeyKeeperStd::State::get_At(Slot::Type iSlot, bool& bAlloc)
    {
        UsedMap::iterator it = m_Used.find(iSlot);
        if (m_Used.end() != it)
        {
            bAlloc = false;
            return &it->second;
        }

        if (!bAlloc)
            return nullptr;

        return &m_Used[iSlot];
    }

    ECC::Hash::Value& LocalPrivateKeyKeeperStd::State::get_AtReady(Slot::Type iSlot)
    {
        bool bAlloc = true;
        ECC::Hash::Value* pVal = get_At(iSlot, bAlloc);
        assert(pVal);

        if (bAlloc)
            Regenerate(*pVal);

        return *pVal;
    }

    void LocalPrivateKeyKeeperStd::get_Nonce(ECC::Scalar::Native& ret, Slot::Type iSlot)
    {
        m_pKdf->DeriveKey(ret, m_State.get_AtReady(iSlot));
    }

    void LocalPrivateKeyKeeperStd::State::Regenerate(ECC::Hash::Value& hv)
    {
        Hash::Processor() << m_hvLast >> m_hvLast;
        hv = m_hvLast;
    }

    void LocalPrivateKeyKeeperStd::State::Regenerate(Slot::Type iSlot)
    {
        // Don't just erase this slot, because the user of our class may depend on occupied slots map
        bool bAlloc = false;
        ECC::Hash::Value* pVal = get_At(iSlot, bAlloc);
        if (pVal)
            Regenerate(*pVal);
    }

    void LocalPrivateKeyKeeperStd::Regenerate(Slot::Type iSlot)
    {
        m_State.Regenerate(iSlot);
    }

} // namespace beam::wallet
