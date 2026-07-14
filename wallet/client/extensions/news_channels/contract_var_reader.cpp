// Copyright 2026 The Beam Team
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

#include "contract_var_reader.h"
#include "contract_price_parse.h"
#include "core/serialization_adapters.h"
#include "utility/serialize.h"

namespace beam::wallet
{
    namespace
    {
        // Paging caps against a malicious/MITM node returning m_bMore=true forever.
        // Sized comfortably above the realistic pool count.
        constexpr uint32_t kMaxPages   = 64;
        constexpr uint32_t kMaxRecords = 4096;

        // cid || KeyTag::Internal(0x00) || subkey...
        ByteBuffer makeKey(const ECC::uintBig& cid, std::initializer_list<uint8_t> tail)
        {
            ByteBuffer k(cid.m_pData, cid.m_pData + cid.nBytes);
            k.push_back(0x00);                 // KeyTag::Internal
            for (uint8_t b : tail) k.push_back(b);
            return k;
        }

        // Parse ContractVars.m_Result records: [nKey][nVal][key][val], like ManagerStd::Vars::MoveNext.
        template <typename F>
        void forEachRecord(const ByteBuffer& buf, F&& f)
        {
            size_t consumed = 0;
            while (consumed < buf.size())
            {
                Deserializer der;
                der.reset(buf.data() + consumed, buf.size() - consumed);
                uint32_t nKey = 0, nVal = 0;
                der & nKey & nVal;
                size_t hdr = (buf.size() - consumed) - der.bytes_left();
                consumed += hdr;
                if (consumed + size_t(nKey) + nVal > buf.size()) break;
                const uint8_t* pKey = buf.data() + consumed; consumed += nKey;
                const uint8_t* pVal = buf.data() + consumed; consumed += nVal;
                f(pKey, nKey, pVal, nVal);
            }
        }

        struct MedianHandler final : public proto::FlyClient::Request::IHandler
        {
            boost::intrusive_ptr<proto::FlyClient::RequestContractVar> m_Req;   // keep request alive
            std::function<void(const OracleMedian&)> m_Cb;
            ~MedianHandler() { if (m_Req) m_Req->m_pTrg = nullptr; }
            void OnComplete(proto::FlyClient::Request& r) override
            {
                r.m_pTrg = nullptr;
                auto& rv = Cast::Up<proto::FlyClient::RequestContractVar>(r);
                OracleMedian om;
                if (!rv.m_Res.m_Value.empty())
                    om = ParseOracleMedianValue(rv.m_Res.m_Value.data(), rv.m_Res.m_Value.size());
                m_Cb(om);
            }
        };

        struct PoolsHandler final : public proto::FlyClient::Request::IHandler
        {
            proto::FlyClient::INetwork::Ptr m_Net;
            boost::intrusive_ptr<proto::FlyClient::RequestContractVars> m_Req;
            std::vector<PoolData> m_Acc;
            std::function<void(std::vector<PoolData>&&)> m_Cb;
            uint32_t m_Pages = 0;

            ~PoolsHandler() { if (m_Req) m_Req->m_pTrg = nullptr; }

            void OnComplete(proto::FlyClient::Request& r) override
            {
                r.m_pTrg = nullptr;
                auto& rv = Cast::Up<proto::FlyClient::RequestContractVars>(r);
                ByteBuffer last;
                forEachRecord(rv.m_Res.m_Result, [&](const uint8_t* k, uint32_t nK, const uint8_t* v, uint32_t nV) {
                    if (m_Acc.size() < kMaxRecords)
                    {
                        PoolData pd;
                        if (ParsePool(k, nK, v, nV, pd) && pd.m_Reserve1 && pd.m_Reserve2)
                            m_Acc.push_back(pd);
                    }
                    last.assign(k, k + nK);
                });
                // Continue paging only when the node's cursor strictly advances within
                // [KeyMin,KeyMax] and the caps are not hit; the enum is unauthenticated,
                // so a malicious node cannot spin us forever or grow m_Acc without bound.
                if (rv.m_Res.m_bMore && !last.empty() && ++m_Pages < kMaxPages &&
                    m_Acc.size() < kMaxRecords &&
                    last > m_Req->m_Msg.m_KeyMin && last <= m_Req->m_Msg.m_KeyMax)
                {
                    m_Req->m_Res = proto::ContractVars();
                    m_Req->m_Msg.m_KeyMin = std::move(last);
                    m_Req->m_Msg.m_bSkipMin = true;
                    m_Net->PostRequest(*m_Req, *this);          // keep paging
                    return;
                }
                m_Cb(std::move(m_Acc));
            }
        };
    }

    void ContractVarReader::ReadMedian(const ECC::uintBig& cid, std::function<void(const OracleMedian&)> cb)
    {
        auto h = std::make_shared<MedianHandler>();
        h->m_Cb = std::move(cb);
        h->m_Req.reset(new proto::FlyClient::RequestContractVar);
        h->m_Req->m_Msg.m_Key = makeKey(cid, {0x00});          // s_Median (exact single key)
        m_Median = h;                                          // retain: handler outlives the round-trip
        m_Net->PostRequest(*h->m_Req, *h);                     // network sets m_pTrg + holds the request
    }

    void ContractVarReader::ReadPools(const ECC::uintBig& cid, std::function<void(std::vector<PoolData>&&)> cb)
    {
        auto h = std::make_shared<PoolsHandler>();
        h->m_Net = m_Net;
        h->m_Cb = std::move(cb);
        h->m_Req.reset(new proto::FlyClient::RequestContractVars);
        h->m_Req->m_Msg.m_KeyMin = makeKey(cid, {0x01, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0});
        h->m_Req->m_Msg.m_KeyMax = makeKey(cid, {0x01, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF});
        m_Pools = h;                                           // retain
        m_Net->PostRequest(*h->m_Req, *h);
    }
}
