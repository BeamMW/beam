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

#pragma once

#ifdef BEAM_SHADERS_USE_STL

#include <array>
#include <algorithm>
#include <iterator>
#include <vector>

#endif // BEAM_SHADERS_USE_STL

#ifdef HOST_BUILD
#include "core/keccak.h"

	#ifdef BEAM_SHADERS_USE_LIBBITCOIN
	#include "bitcoin/bitcoin/math/elliptic_curve.hpp"
	#endif // BEAM_SHADERS_USE_LIBBITCOIN

namespace Shaders {
#endif

#include "common.h"

namespace Eth
{
	using Address = Opaque<20>;
	using Hash = Opaque<32>;

	void MemCopy(void* dest, const void* src, uint32_t n)
	{
#ifdef HOST_BUILD
		memcpy(dest, src, n);
#else // HOST_BUILD
		Env::Memcpy(dest, src, n);
#endif // HOST_BUILD
	}

	int MemCmp(const void* dest, const void* src, uint32_t n)
	{
#ifdef HOST_BUILD
		return memcmp(dest, src, n);
#else // HOST_BUILD
		return Env::Memcmp(dest, src, n);
#endif // HOST_BUILD
	}

	template <uint32_t N>
	constexpr uint32_t strlen(char const (&s)[N])
	{
		return N - 1;
	}

	Opaque<1> to_opaque(char s)
	{
		Opaque<1> r;
		MemCopy(&r, &s, 1);
		return r;
	}

	template<uint32_t N>
	auto to_opaque(char const (&s)[N])
	{
		constexpr auto size = sizeof(char) * (N - 1);
		Opaque<size> r;
		MemCopy(&r, &s, size);
		return r;
	}

	struct Rlp
	{
		struct Node
		{
			enum struct Type
			{
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
			} ;

			template <uint32_t nBytes>
			explicit Node(const Opaque<nBytes>& hv)
				: m_Type(Type::String)
				, m_nLen(nBytes)
				, m_pBuf(reinterpret_cast<const uint8_t*>(&hv))
			{
			}

			Node() = default;

			explicit Node(uint64_t n)
				: m_Type(Type::Integer)
				, m_nLen(0)
				, m_Integer(n)
			{
			}

#ifdef BEAM_SHADERS_USE_STL

			template <size_t nBytes>
			explicit Node(const std::array<uint8_t, nBytes>& hv)
				: Node(Type::String, hv.data(), nBytes)
			{

			}

			explicit Node(const std::vector<uint8_t>& hv)
				: Node(Type::String, hv.data(), hv.size())
			{
			}

			template<typename T>
			bool operator==(T&& v) const
			{
				return m_Type == Type::String 
					&& std::equal(begin(v), end(v), m_pBuf, m_pBuf + m_nLen);
			}

#endif // BEAM_SHADERS_USE_STL

			Node(Type type, const uint8_t* data, size_t size)
				: m_Type(type)
				, m_nLen(static_cast<uint32_t>(size))
				, m_pBuf(data)
			{
			}

			template <uint32_t N>
			Node(const Node(&nodes)[N])
				: m_Type(Type::List)
				, m_nLen(N)
				, m_pC(nodes)
			{
			}

            template <uint32_t nBytes>
			void Set(const Opaque<nBytes>& hv)
			{
				m_Type = Type::String;
				m_nLen = nBytes;
				m_pBuf = reinterpret_cast<const uint8_t*>(&hv);
			}

			void Set(uint64_t n)
			{
				m_Type = Type::Integer;
				m_Integer = n;
			}

			static constexpr uint8_t get_BytesFor(uint64_t n)
			{
				uint8_t nLen = 0;
				while (n)
				{
					n >>= 8;
					nLen++;

				}
				return nLen;
			}

			Type type() const
			{
				return m_Type;
			}

			const uint8_t* data() const
			{
				return m_pBuf;
			}

			size_t size() const
			{
				return m_nLen;
			}

			bool empty() const
			{
				return m_nLen == 0;
			}

			void EnsureSizeBrutto() const
			{
				if (!m_SizeBrutto)
				{
					struct SizeCounter
					{
						uint64_t m_Val = 0;
						void Write(uint8_t) { m_Val++; }
						void Write(const void*, size_t nLen) { m_Val += nLen; }
					} sc;

					Write(sc);
					m_SizeBrutto = sc.m_Val;
				}
			}

			template <typename TStream>
			static void WriteVarLen(TStream& s, uint64_t n, uint8_t nLen)
			{
				for (nLen <<= 3; nLen; )
				{
					nLen -= 8;
					s.Write(static_cast<uint8_t>(n >> nLen));
				}
			}

			template <typename TStream>
			void WriteSize(TStream& s, uint8_t nBase, uint64_t n) const
			{
				if (n < 56)
					s.Write(nBase + static_cast<uint8_t>(n));
				else
				{
					uint8_t nLen = get_BytesFor(n);
					s.Write(nBase + 55 + nLen);
					WriteVarLen(s, n, nLen);
				}
			}

			template <typename TStream>
			void Write(TStream& s) const
			{
				switch (m_Type)
				{
				case Type::List:
				{
					uint64_t nChildren = 0;

					for (uint32_t i = 0; i < m_nLen; i++)
					{
						m_pC[i].EnsureSizeBrutto();
						nChildren += m_pC[i].m_SizeBrutto;
					}

					WriteSize(s, 0xc0, nChildren);

					for (uint32_t i = 0; i < m_nLen; i++)
						m_pC[i].Write(s);

				}
				break;

				case Type::String:
				{
					if (m_nLen != 1 || m_pBuf[0] >= 0x80)
					{
						WriteSize(s, 0x80, m_nLen);
					}
					s.Write(m_pBuf, m_nLen);
				}
				break;

				default:
					assert(false);
					// no break;

				case Type::Integer:
				{
					if (m_Integer && m_Integer < 0x80)
					{
						s.Write(static_cast<uint8_t>(m_Integer));
					}
					else
					{
						uint8_t nLen = get_BytesFor(m_Integer);
						WriteSize(s, 0x80, nLen);
						WriteVarLen(s, m_Integer, nLen);
					}
				}
				}
			}
		};

		template<typename Visitor>
		static bool Decode(const uint8_t* input, size_t size, Visitor& visitor)
		{
			size_t position = 0;
			auto decodeInteger = [&](uint8_t bytes, size_t& length)
			{
				if (bytes > size - position)
					return false;
				length = 0;
				while (bytes--)
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
					visitor.OnNode({ Node::Type::String, &input[position - 1], 1 });
				}
				else
				{
					size_t length = 0;
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
		static void DecodeString(const uint8_t* input, size_t size, Visitor& visitor)
		{
			visitor.OnNode(Rlp::Node(Node::Type::String, input, size));
		}

		template <typename Visitor>
		static bool DecodeList(const uint8_t* input, size_t size, Visitor& visitor)
		{
			if (visitor.OnNode(Rlp::Node(Node::Type::List, input, size)))
			{
				return Decode(input, size, visitor);
			}
			return true;
		}

		struct HashStream
		{

#ifdef HOST_BUILD
			beam::KeccakProcessor<256> m_hp;
#else // HOST_BUILD
			HashProcessor::Base m_hp;

			HashStream() {
				m_hp.m_p = Env::HashCreateKeccak(256);
			}
#endif // HOST_BUILD
		
			void Write(uint8_t x)
			{
				if (m_nBuf == _countof(m_pBuf))
					FlushStrict();

				m_pBuf[m_nBuf++] = x;

			}

			void Write(const uint8_t* p, uint32_t n)
			{
				if (!Append(p, n))
				{
					Flush();
					if (!Append(p, n))
						m_hp.Write(p, n);
				}
			}

			template <typename T>
			void operator >> (T& res)
			{
				if (m_nBuf)
					FlushStrict();
				m_hp >> res;
			}

		private:

			uint8_t m_pBuf[128];
			uint32_t m_nBuf = 0;

			void Flush()
			{
				if (m_nBuf)
					FlushStrict();
			}

			void FlushStrict()
			{
				m_hp.Write(m_pBuf, m_nBuf);
				m_nBuf = 0;
			}

			bool Append(const uint8_t* p, uint32_t n)
			{
				if (m_nBuf + n > _countof(m_pBuf))
					return false;

#ifdef HOST_BUILD
				memcpy(m_pBuf + m_nBuf, p, n);
#else // HOST_BUILD
				Env::Memcpy(m_pBuf + m_nBuf, p, n);
#endif // HOST_BUILD

				m_nBuf += n;
				return true;
			}
		};
	};

	struct Header
	{
		Opaque<32> m_ParentHash;
		Opaque<32> m_UncleHash;
		Opaque<20> m_Coinbase;
		Opaque<32> m_Root;
		Opaque<32> m_TxHash;
		Opaque<32> m_ReceiptHash;
		Opaque<256> m_Bloom;
		Opaque<32> m_Extra;
		uint32_t m_nExtra; // can be less than maximum size

		uint64_t m_Difficulty;
		uint64_t m_Number; // height
		uint64_t m_GasLimit;
		uint64_t m_GasUsed;
		uint64_t m_Time;
		uint64_t m_Nonce;
		uint64_t m_BaseFeePerGas = 0;

		void get_HashForPow(Opaque<32>& hv) const
		{
			get_HashInternal(hv, nullptr);
		}

		void get_HashFinal(Opaque<32>& hv, const Opaque<32>& hvMixHash) const
		{
			get_HashInternal(hv, &hvMixHash);
		}

		void get_SeedForPoW(Opaque<64>& hv) const
		{
			Opaque<32> x;
			get_HashForPow(x);

#ifdef HOST_BUILD
			beam::KeccakProcessor<512> hp;
#else // HOST_BUILD
			HashProcessor::Base hp;
			hp.m_p = Env::HashCreateKeccak(512);
#endif // HOST_BUILD

			hp << x;

			auto nonce = Utils::FromLE(m_Nonce);
			hp.Write(&nonce, sizeof(nonce));

			hp >> hv;
		}

		uint32_t get_Epoch() const
		{
			return static_cast<uint32_t>(m_Number / 30000);
		}

	private:

		void get_HashInternal(Opaque<32>& hv, const Opaque<32>* phvMix) const
		{
			Rlp::Node pN[16];
			pN[0].Set(m_ParentHash);
			pN[1].Set(m_UncleHash);
			pN[2].Set(m_Coinbase);
			pN[3].Set(m_Root);
			pN[4].Set(m_TxHash);
			pN[5].Set(m_ReceiptHash);
			pN[6].Set(m_Bloom);
			pN[7].Set(m_Difficulty);
			pN[8].Set(m_Number);
			pN[9].Set(m_GasLimit);
			pN[10].Set(m_GasUsed);
			pN[11].Set(m_Time);
			pN[12].Set(m_Extra);
			pN[12].m_nLen = m_nExtra;

			Rlp::Node nRoot;
			nRoot.m_Type = Rlp::Node::Type::List;
			nRoot.m_pC = pN;
			if (phvMix)
			{
				pN[13].Set(*phvMix);
				pN[14].Set(m_Nonce);
				nRoot.m_nLen = 15;
			}
			else
				nRoot.m_nLen = 13;

			if (m_BaseFeePerGas)
			{
				pN[nRoot.m_nLen].Set(m_BaseFeePerGas);
				nRoot.m_nLen += 1;
			}

			Rlp::HashStream hs;
			nRoot.Write(hs);
			hs >> hv;
		}
	};

	uint8_t GetRLPPrefixSize(const Rlp::Node& node)
	{
		if (node.m_nLen > 55)
			return 1 + Rlp::Node::get_BytesFor(node.m_nLen);

		return (node.m_nLen == 1 && node.m_pBuf[0] <= 0x7f) ? 0 : 1;
	}

	void TriePathToNibbles(const uint8_t* path, uint32_t pathLength,
		uint8_t* nibbles, uint32_t nibblesLength)
	{
		assert(nibblesLength != pathLength);
		// to nibbles
		for (uint32_t i = 0; i < pathLength; i++)
		{
			nibbles[i * 2] = path[i] >> 4;
			nibbles[i * 2 + 1] = path[i] & 15;
		}
	}

	// Validates prefix, compares "sharedNibbles(or key-end if leaf node)" and returns TrieKey offset
	int RemovePrefix(const uint8_t* encodedPath, uint32_t encodedPathLength,
					 const uint8_t* key, uint32_t keyLength, uint8_t keyPos)
	{
		// encodedPath to nibbles
		uint8_t* nibbles = nullptr;
		uint32_t nibblesLength = encodedPathLength * 2;

#ifdef HOST_BUILD
		auto tmp = std::make_unique<uint8_t[]>(nibblesLength);
		nibbles = tmp.get();
#else // HOST_BUILD
		nibbles = (uint8_t*)Env::StackAlloc(nibblesLength);
#endif // HOST_BUILD

		TriePathToNibbles(encodedPath, encodedPathLength, nibbles, nibblesLength);

		// checking prefix
		/* 0 - even extension node
		 * 1 - odd extension node
		 * 2 - even leaf node
		 * 3 - odd leaf node
		*/
		assert(nibbles[0] <= 3 && "The proof has the incorrect format!");

		// even extension node OR even leaf node -> skips 2 nibbles
		uint8_t offset = (nibbles[0] == 0 || nibbles[0] == 2) ? 2 : 1;

		// checking that key contains nibbles
		if (!MemCmp(nibbles + offset, key + keyPos, nibblesLength - offset))
		{
			return nibblesLength - offset;
		}

		assert(false && "encodedPath not found in the key");
		return -1;
	}

	struct RlpVisitor
	{
		explicit RlpVisitor(uint8_t maxNested = 2)
			: m_MaxNested(maxNested)
		{}
		bool OnNode(const Rlp::Node& node)
		{
			auto& item = m_Items.emplace_back();
			item = node;
			m_Nested++;
			return m_Nested < m_MaxNested;
		}

		uint32_t ItemsCount() const
		{
#ifdef HOST_BUILD
			return static_cast<uint32_t>(m_Items.size());
#else // HOST_BUILD
			return static_cast<uint32_t>(m_Items.m_Count);
#endif // HOST_BUILD
		}

		const Rlp::Node& GetItem(uint32_t index) const
		{
#ifdef HOST_BUILD
			return m_Items[index];
#else // HOST_BUILD
			return m_Items.m_p[index];
#endif // HOST_BUILD
		}

		uint8_t m_Nested = 0;
		uint8_t m_MaxNested = 2;
#ifdef HOST_BUILD
		std::vector<Rlp::Node> m_Items;
#else // HOST_BUILD
		Utils::Vector<Rlp::Node> m_Items;
#endif // HOST_BUILD

	};

	bool VerifyEthProof(const uint8_t* trieKey, uint32_t trieKeySize,
						const uint8_t* proof, uint32_t proofSize,
						const HashValue& rootHash,
						uint8_t** out, uint32_t& outSize)
	{
		RlpVisitor rootVisitor;
		Rlp::Decode(proof, proofSize, rootVisitor);
		const uint8_t* newExpectedRoot = reinterpret_cast<const uint8_t*>(&rootHash);
		uint8_t keyPos = 0;
		HashValue nodeHash;

		for (uint32_t i = 1; i < rootVisitor.ItemsCount(); i++)
		{
			// TODO: is always a hash? check some samples with currentNode.size() < 32 bytes
			RlpVisitor visitor;
			Rlp::Decode(rootVisitor.GetItem(i).m_pBuf, rootVisitor.GetItem(i).m_nLen, visitor);

#ifdef HOST_BUILD
			beam::KeccakProcessor<256> hp;
#else // HOST_BUILD
			HashProcessor::Base hp;
			hp.m_p = Env::HashCreateKeccak(256);
#endif // HOST_BUILD
			
			// For hash calculation used full data: prefix(type + length) + body
			uint32_t prefixOffset = GetRLPPrefixSize(rootVisitor.GetItem(i));

			hp.Write(rootVisitor.GetItem(i).m_pBuf - prefixOffset, rootVisitor.GetItem(i).m_nLen + prefixOffset);
			hp >> nodeHash;

			if (MemCmp(newExpectedRoot, reinterpret_cast<const uint8_t*>(&nodeHash), 32))
			{
				return false;
			}

			if (keyPos > trieKeySize)
			{
				return false;
			}
			switch (visitor.ItemsCount())
			{
				// branch node
				case 17:
				{
					if (keyPos == trieKeySize)
					{
						if (i == rootVisitor.ItemsCount() - 1)
						{
							// value stored in the branch
							*out = const_cast<uint8_t*>(visitor.GetItem(visitor.ItemsCount() - 1).m_pBuf);
							outSize = visitor.GetItem(visitor.ItemsCount() - 1).m_nLen;
							return true;
						}
						else
							assert(false && "i != proof.size() - 1");

						return false;
					}

					newExpectedRoot = visitor.GetItem(trieKey[keyPos]).m_pBuf;
					keyPos += 1;
					break;
				}
				// leaf or extension node
				case 2:
				{
					int offset = RemovePrefix(visitor.GetItem(0).m_pBuf,
											  visitor.GetItem(0).m_nLen,
											  trieKey, trieKeySize, keyPos);

					if (offset == -1)
						return false;

					keyPos += static_cast<uint8_t>(offset);
					if (keyPos == trieKeySize)
					{
						// leaf node
						if (i == rootVisitor.ItemsCount() - 1)
						{
							*out = const_cast<uint8_t*>(visitor.GetItem(1).m_pBuf);
							outSize = visitor.GetItem(1).m_nLen;
							return true;
						}
						return false;
					}
					else
					{
						// extension node
						newExpectedRoot = visitor.GetItem(1).m_pBuf;
					}
					break;
				}
				default:
				{
					return false;
				}
			}
		}
		return false;
	}

	using Signature = Opaque<64>;
	using RecoverableSignature = Opaque<65>;
	using PublicKey = Opaque<33>;
	using Amount = Opaque<16>;

#ifdef BEAM_SHADERS_USE_STL
	template<typename Cont, uint32_t N>
	bool operator==(const Opaque<N>& o, const Cont& c)
	{
		return std::equal(begin(o), end(o), begin(c), end(c));
	}

	template<typename Cont, uint32_t N>
	bool operator==(const Cont& c, const Opaque<N>& o)
	{
		return std::equal(begin(o), end(o), begin(c), end(c));
	}
#endif

	struct RawTransactionData
	{
		Hash			messageHash = {0};
		Address			recipient = {0};
		Amount			amount = {0};
		Signature		signature = {0};
		uint8_t			recoveryID = {0};
#ifdef BEAM_SHADERS_USE_STL
		std::vector<uint8_t> data;
#endif
	};

	bool ExtractDataFromRawTransaction(RawTransactionData& txData, const uint8_t* data, size_t size)
	{
#ifdef BEAM_SHADERS_USE_STL
		RlpVisitor rootVisitor(1);
		Rlp::Decode(data, size, rootVisitor);

		if (rootVisitor.ItemsCount() != 1)
			return false;

		RlpVisitor listVisitor(1);
		Rlp::Decode(rootVisitor.GetItem(0).data(), rootVisitor.GetItem(0).size(), listVisitor);

		if (listVisitor.ItemsCount() != 9)
			return false;

		auto myCopy = [&](const Rlp::Node& node, auto dest)
		{
			std::copy(node.data(), node.data() + node.size(), dest);
		};


		myCopy(listVisitor.GetItem(3), begin(txData.recipient));
		myCopy(listVisitor.GetItem(4), std::next(begin(txData.amount), Amount::nBytes - listVisitor.GetItem(4).size()));
#ifdef BEAM_SHADERS_USE_STL
		myCopy(listVisitor.GetItem(5), std::back_inserter(txData.data));
#endif
		Rlp::Node nodes[9] =
		{
			listVisitor.GetItem(0), // nonce
			listVisitor.GetItem(1), // gas price
			listVisitor.GetItem(2), // gas limit
			listVisitor.GetItem(3), // recipient(EOA, Contract Account)
			listVisitor.GetItem(4), // value
			listVisitor.GetItem(5), // data
			Rlp::Node{1410},		// derived chain id, V
			Rlp::Node(Rlp::Node::Type::String, nullptr, 0), // R 
			Rlp::Node(Rlp::Node::Type::String, nullptr, 0), // S
		};
		Rlp::Node tx(nodes);
		Rlp::HashStream hs;
		tx.Write(hs);
		hs >> txData.messageHash;
		myCopy(listVisitor.GetItem(7), std::next(begin(txData.signature), 0));		// R
		myCopy(listVisitor.GetItem(8), std::next(begin(txData.signature), 32));		// S

		int i = 0;
		const auto& n = listVisitor.GetItem(6);
		for (size_t j = 0; j < n.size(); ++j) // V
		{
			i <<= 8;
			i += n.data()[j];
		}

		i = i - 1410 * 2 - 35;
		txData.recoveryID = uint8_t(i);
		return true;
#else
		return false;
#endif
	}

	bool ExtractPubKeyFromSignature(PublicKey& pubKey, const Hash& hash, const Signature& signature, uint8_t recoveryID)
	{
#if defined(HOST_BUILD) && defined(BEAM_SHADERS_USE_LIBBITCOIN)
		libbitcoin::recoverable_signature recSig;
		std::copy(begin(signature), end(signature), begin(recSig.signature));
		recSig.recovery_id = recoveryID;

		libbitcoin::ec_compressed pub;
		libbitcoin::hash_digest h;
		std::copy(begin(hash), end(hash), h.begin());
		if (!libbitcoin::recover_public(pub, recSig, h))
			return false;

		std::copy(begin(pub), end(pub), begin(pubKey));
		return true;
#else
		auto p = reinterpret_cast<const uint8_t*>(&signature);
		Secp::Scalar r, s, m;
		if (!r.Import(*reinterpret_cast<const Secp_scalar_data*>(p)) ||
			!s.Import(*reinterpret_cast<const Secp_scalar_data*>(p + 32)) ||
			!m.Import(*reinterpret_cast<const Secp_scalar_data*>(&hash)))
		{
			return false;
		}

		PubKey tx;
		Env::Memcpy(&tx.m_X, p, 32);
		tx.m_Y = recoveryID & 1; // odd

		Secp::Point R, Q, p1;

		if (!R.Import(tx))
		{
			return false;
		}

		Secp::Scalar rn, u1, u2;
		Env::Secp_Scalar_inv(rn, r);
		u1 = rn * m;
		u1 = -u1;
		u2 = rn * s;
		Env::Secp_Point_mul_G(Q, u1);
		Env::Secp_Point_mul(p1, R, u2);

		Q += p1;
		if (Q.IsZero())
		{
			return false;
		}

		Q.Export(tx);
		auto p2 = reinterpret_cast<uint8_t*>(&pubKey);
		Env::Memcpy(p2 + 1, &tx.m_X, 32);
		p2[0] = tx.m_Y ? 0x03 : 0x02;

		return true;
#endif
	}


	Address ToAddress(const PublicKey& pubKey)
	{
		Address address;
#if defined(HOST_BUILD) && defined(BEAM_SHADERS_USE_LIBBITCOIN)
		libbitcoin::ec_compressed pub;
		libbitcoin::ec_uncompressed upub;
		std::copy(begin(pubKey), end(pubKey), begin(pub));
		libbitcoin::decompress(upub, pub);
		auto addr = ethash::keccak256(upub.data() + 1, upub.size() - 1);
		std::copy_n(addr.bytes + 12, Address::nBytes, begin(address));

#endif
		return address;
	}

	Address ToAddress(const PublicKey& pubKey, const Secp_scalar_data& keyY)
	{
		Address address;
		Rlp::HashStream hs;
		hs.Write(reinterpret_cast<const uint8_t*>(&pubKey) + 1, 32);
		hs.Write(reinterpret_cast<const uint8_t*>(&keyY), 32);

		Hash hash;
		hs >> hash;
		std::copy_n(std::next(begin(hash), 12), Address::nBytes, begin(address));
		return address;
	}

	bool VerifyTransactionSignature(const PublicKey& pubKey, const Hash& hash, const Signature& signature)
	{
#if defined(HOST_BUILD) && defined(BEAM_SHADERS_USE_LIBBITCOIN)
		libbitcoin::ec_compressed pub2;
		libbitcoin::ec_signature sig;
		libbitcoin::hash_digest h;
		std::copy(begin(pubKey), end(pubKey), begin(pub2));
		std::copy(begin(signature), end(signature), begin(sig));
		std::copy(begin(hash), end(hash), begin(h));
		return libbitcoin::verify_signature2(pub2, h, sig);
#else
		const uint8_t* p = reinterpret_cast<const uint8_t*>(&pubKey);
		if (p[0] != 0x02 && p[0] != 0x03)
			return false;

		PubKey pubCopy;
		Env::Memcpy(&pubCopy.m_X, p + 1, 32);
		pubCopy.m_Y = p[0] == 0x03; // odd

		p = reinterpret_cast<const uint8_t*>(&signature);
		Secp::Scalar r, s, m;
		Secp::Point pub, p0, p1;
		if (!r.Import(*reinterpret_cast<const Secp_scalar_data*>(p)) ||
			!s.Import(*reinterpret_cast<const Secp_scalar_data*>(p + 32)) ||
			!m.Import(*reinterpret_cast<const Secp_scalar_data*>(&hash)) ||
			!pub.Import(pubCopy))
			return false;

		Secp::Scalar sn, u1, u2;

		Env::Secp_Scalar_inv(sn, s);
		u1 = sn * m;
		u2 = sn * r;

		Env::Secp_Point_mul_G(p0, u1);
		Env::Secp_Point_mul(p1, pub, u2);
		p0 += p1;  // u1 * G + u2 * Q

		if (p0.IsZero())
			return false;

		PubKey k1;
		p0.Export(k1);

		Secp::Scalar t;
		if (!t.Import(*reinterpret_cast<const Secp_scalar_data*>(&k1)))
			return false;

		r = -r;
		t += r;
		Secp_scalar_data tdata;
		t.Export(tdata);
		return Env::Memis0(&tdata, sizeof(tdata));
#endif
	}
}

#ifdef HOST_BUILD
} // namespace Shaders
#endif