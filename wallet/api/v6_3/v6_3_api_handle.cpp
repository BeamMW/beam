// Copyright 2018 The Beam Team
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
#include "v6_3_api.h"
#include "version.h"

#include <string_view>

namespace beam::wallet
{
    namespace
    {
        struct Rlp
        {
            struct Node
            {
                enum struct Type {
                    List,
                    String,
                    Integer,
                };

                Type m_Type;
                mutable uint64_t m_SizeBrutto = 0; // cached
                uint32_t m_nLen; // for strings and lists

                union {
                    const Node* m_pC;
                    const uint8_t* m_pBuf;
                    uint64_t m_Integer;
                };

                Node() = default;

                explicit Node(uint64_t n)
                    : m_Type(Type::Integer)
                    , m_nLen(0)
                    , m_Integer(n)
                {
                }

                template <uint32_t N>
                Node(const Node(&nodes)[N])
                    : m_Type(Type::List)
                    , m_nLen(N)
                    , m_pC(nodes)
                {
                }

                //template <uint32_t nBytes>
                //void Set(const Opaque<nBytes>& hv)
                //{
                //    m_Type = Type::String;
                //    m_nLen = nBytes;
                //    m_pBuf = reinterpret_cast<const uint8_t*>(&hv);
                //}

                //void Set(uint64_t n)
                //{
                //    m_Type = Type::Integer;
                //    m_Integer = n;
                //}

                //static constexpr uint8_t get_BytesFor(uint64_t n)
                //{
                //    uint8_t nLen = 0;
                //    while (n)
                //    {
                //        n >>= 8;
                //        nLen++;

                //    }
                //    return nLen;
                //}

                //void EnsureSizeBrutto() const
                //{
                //    if (!m_SizeBrutto)
                //    {
                //        struct SizeCounter {
                //            uint64_t m_Val = 0;
                //            void Write(uint8_t) { m_Val++; }
                //            void Write(const void*, uint32_t nLen) { m_Val += nLen; }
                //        } sc;

                //        Write(sc);
                //        m_SizeBrutto = sc.m_Val;
                //    }
                //}

                //template <typename TStream>
                //static void WriteVarLen(TStream& s, uint64_t n, uint8_t nLen)
                //{
                //    for (nLen <<= 3; nLen; )
                //    {
                //        nLen -= 8;
                //        s.Write(static_cast<uint8_t>(n >> nLen));
                //    }
                //}

                //template <typename TStream>
                //void WriteSize(TStream& s, uint8_t nBase, uint64_t n) const
                //{
                //    if (n < 56)
                //        s.Write(nBase + static_cast<uint8_t>(n));
                //    else
                //    {
                //        uint8_t nLen = get_BytesFor(n);
                //        s.Write(nBase + 55 + nLen);
                //        WriteVarLen(s, n, nLen);
                //    }
                //}

                //template <typename TStream>
                //void Write(TStream& s) const
                //{
                //    switch (m_Type)
                //    {
                //    case Type::List:
                //    {
                //        uint64_t nChildren = 0;

                //        for (uint32_t i = 0; i < m_nLen; i++)
                //        {
                //            m_pC[i].EnsureSizeBrutto();
                //            nChildren += m_pC[i].m_SizeBrutto;
                //        }

                //        WriteSize(s, 0xc0, nChildren);

                //        for (uint32_t i = 0; i < m_nLen; i++)
                //            m_pC[i].Write(s);

                //    }
                //    break;

                //    case Type::String:
                //    {
                //        if (m_nLen != 1 || m_pBuf[0] >= 0x80)
                //        {
                //            WriteSize(s, 0x80, m_nLen);
                //        }
                //        s.Write(m_pBuf, m_nLen);
                //    }
                //    break;

                //    default:
                //        assert(false);
                //        // no break;

                //    case Type::Integer:
                //    {
                //        if (m_Integer && m_Integer < 0x80)
                //        {
                //            s.Write(static_cast<uint8_t>(m_Integer));
                //        }
                //        else
                //        {
                //            uint8_t nLen = get_BytesFor(m_Integer);
                //            WriteSize(s, 0x80, nLen);
                //            WriteVarLen(s, m_Integer, nLen);
                //        }
                //    }
                //    }
                //}
            };


            template<typename Visitor>
            static bool Decode(const uint8_t* input, uint32_t size, Visitor& visitor)
            {
                uint32_t position = 0;
                auto decodeInteger = [&](uint8_t nBytes, uint32_t& length)
                {
                    if (nBytes > size - position)
                        return false;
                    length = 0;
                    while (nBytes--)
                    {
                        length = input[position++] + length * 256;
                    }
                    return true;
                };

                while (position < size)
                {
                    auto b = input[position++];
                    if (b <= 0x7f)  // single byte
                    {
                        Rlp::Node n;
                        n.m_Type = Rlp::Node::Type::String;
                        n.m_nLen = 1;
                        n.m_pBuf = &input[position++];
                        visitor.OnNode(n);
                    }
                    else
                    {
                        uint32_t length = 0;
                        if (b <= 0xb7) // short string
                        {
                            length = b - 0x80;
                            if (length > size - position)
                                return false;
                            DecodeString(input + position, length, visitor);
                        }
                        else if (b <= 0xbf) // long string
                        {
                            if (!decodeInteger(b - 0xb7, length) || length > size - position)
                                return false;
                            DecodeString(input + position, length, visitor);
                        }
                        else if (b <= 0xf7) // short list
                        {
                            length = b - 0xc0;
                            if (length > size - position)
                                return false;
                            if (!DecodeList(input + position, length, visitor))
                                return false;
                        }
                        else if (b <= 0xff) // long list
                        {
                            if (!decodeInteger(b - 0xf7, length) || length > size - position)
                                return false;
                            if (!DecodeList(input + position, length, visitor))
                                return false;
                        }
                        else
                        {
                            return false;
                        }
                        position += length;
                    }
                }
                return true;
            }

            template <typename Visitor>
            static void DecodeString(const uint8_t* input, uint32_t size, Visitor& visitor)
            {
                Rlp::Node n;
                n.m_Type = Rlp::Node::Type::String;
                n.m_nLen = size;
                n.m_pBuf = input;
                visitor.OnNode(n);
            }

            template <typename Visitor>
            static bool DecodeList(const uint8_t* input, uint32_t size, Visitor& visitor)
            {
                Rlp::Node n;
                n.m_Type = Rlp::Node::Type::List;
                n.m_nLen = size;
                n.m_pBuf = input;
                if (visitor.OnNode(n))
                {
                    return Decode(input, size, visitor);
                }
                return true;
            }
        };
    }


    void V63Api::onHandleChainID(const JsonRpcId& id, ChainID&& req)
    {
        ChainID::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleNetVersion(const JsonRpcId& id, NetVersion&& req)
    {
        NetVersion::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleBlockNumber(const JsonRpcId& id, BlockNumber&& req)
    {
        BlockNumber::Response res;

        res.height = get_TipHeight();
        doResponse(id, res);
    }

    void V63Api::onHandleBalance(const JsonRpcId& id, Balance&& req)
    {
        Balance::Response res;
        auto walletDB = getWalletDB();
        storage::Totals allTotals(*walletDB, false);
        const auto& totals = allTotals.GetBeamTotals();
        auto b = totals.Avail;
        b += totals.AvailShielded;
        uintBig_t<sizeof(Amount)> m;
        m = 10'000'000'000UL;
        b = b * m ;
        
        res.balanceHi = AmountBig::get_Hi(b);
        res.balanceLo = AmountBig::get_Lo(b);

        doResponse(id, res);
    }

    void V63Api::onHandleBlockByNumber(const JsonRpcId& id, BlockByNumber&& req)
    {
        BlockByNumber::Response res;
        res.number = get_TipHeight();
        doResponse(id, res);
    }

    void V63Api::onHandleGasPrice(const JsonRpcId& id, GasPrice&& req)
    {
        GasPrice::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleEstimateGas(const JsonRpcId& id, EstimateGas&& req)
    {
        EstimateGas::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleGetCode(const JsonRpcId& id, GetCode&& req)
    {
        GetCode::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleGetTransactionCount(const JsonRpcId& id, GetTransactionCount&& req)
    {
        GetTransactionCount::Response res;
        doResponse(id, res);
    }

    namespace
    {
        struct RlpVisitor
        {
            struct Node
            {
                Rlp::Node::Type m_Type;
                ByteBuffer m_Buffer;
            };

            bool OnNode(const Rlp::Node& node)
            {
                auto& item = m_Items.emplace_back();
                item.m_Type = node.m_Type;
                item.m_Buffer.assign(node.m_pBuf, node.m_pBuf + node.m_nLen);
                return false;
            }


            std::vector<Node> m_Items;
        };
    }

    void V63Api::onHandleSendRawTransaction(const JsonRpcId& id, SendRawTransaction&& req)
    {
        SendRawTransaction::Response res;

        RlpVisitor v;
        Rlp::Decode(req.rawTransaction.data(), (uint32_t)req.rawTransaction.size(), v);

        RlpVisitor v2;
        Rlp::Decode(v.m_Items[0].m_Buffer.data(), (uint32_t)v.m_Items[0].m_Buffer.size(), v2);
        const auto& address = v2.m_Items[2].m_Buffer;
        AmountBig::Type amount{Blob(v2.m_Items[3].m_Buffer)};
        address;
        amount;

        doResponse(id, res);
    }

    void V63Api::onHandleGetTransactionReceipt(const JsonRpcId& id, GetTransactionReceipt&& req)
    {
        GetTransactionReceipt::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleGetBlockByHash(const JsonRpcId& id, GetBlockByHash&& req)
    {
        GetBlockByHash::Response res;
        res.number = get_TipHeight();
        doResponse(id, res);
    }

    /////


    void V63Api::onHandleIPFSAdd(const JsonRpcId &id, IPFSAdd&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_add(std::move(req.data), req.pin, req.timeout,
            [this, id, pin = req.pin, wguard = _weakSelf](std::string&& hash) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                IPFSAdd::Response response = {hash, pin};
                doResponse(id, response);
            },
            [this, id, wguard = _weakSelf] (std::string&& err) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                sendError(id, ApiError::IPFSError, err);
            }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    void V63Api::onHandleIPFSHash(const JsonRpcId &id, IPFSHash&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_hash(std::move(req.data), req.timeout,
             [this, id, wguard = _weakSelf](std::string&& hash) {
                 auto guard = wguard.lock();
                 if (!guard)
                 {
                     LOG_WARNING() << "API destroyed before IPFS response received.";
                     return;
                 }

                 IPFSHash::Response response = {hash};
                 doResponse(id, response);
             },
             [this, id, wguard = _weakSelf] (std::string&& err) {
                 auto guard = wguard.lock();
                 if (!guard)
                 {
                     LOG_WARNING() << "API destroyed before IPFS response received.";
                     return;
                 }

                 sendError(id, ApiError::IPFSError, err);
             }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    void V63Api::onHandleIPFSGet(const JsonRpcId &id, IPFSGet&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_get(req.hash, req.timeout,
        [this, id, hash = req.hash, wguard = _weakSelf](std::vector<uint8_t>&& data) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                IPFSGet::Response response = {hash, std::move(data)};
                doResponse(id, response);
            },
            [this, id, wguard = _weakSelf] (std::string&& err) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                sendError(id, ApiError::IPFSError, err);
            }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    void V63Api::onHandleIPFSPin(const JsonRpcId &id, IPFSPin&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_pin(req.hash, req.timeout,
        [this, id, hash = req.hash, wguard = _weakSelf]() {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                IPFSPin::Response response = {hash};
                doResponse(id, response);
            },
            [this, id, wguard = _weakSelf] (std::string&& err) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }
                sendError(id, ApiError::IPFSError, err);
            }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    void V63Api::onHandleIPFSUnpin(const JsonRpcId &id, IPFSUnpin&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_unpin(req.hash,
          [this, id, hash = req.hash, wguard = _weakSelf]() {
              auto guard = wguard.lock();
              if (!guard)
              {
                  LOG_WARNING() << "API destroyed before IPFS response received.";
                  return;
              }

              IPFSUnpin::Response response = {hash};
              doResponse(id, response);
          },
          [this, id, wguard = _weakSelf] (std::string&& err) {
              auto guard = wguard.lock();
              if (!guard)
              {
                  LOG_WARNING() << "API destroyed before IPFS response received.";
                  return;
              }
              sendError(id, ApiError::IPFSError, err);
          }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    void V63Api::onHandleIPFSGc(const JsonRpcId &id, IPFSGc&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_gc(req.timeout,
            [this, id, wguard = _weakSelf]() {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                IPFSGc::Response response;
                doResponse(id, response);
            },
            [this, id, wguard = _weakSelf] (std::string&& err) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }
                sendError(id, ApiError::IPFSError, err);
            }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }
}
