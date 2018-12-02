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

#include "block_crypt.h"
#include "../utility/serialize.h"

namespace yas
{
namespace detail
{
    // unique_ptr adapter (copied from serialize_test.cpp)
    template<std::size_t F, typename T>
    struct serializer<
        type_prop::not_a_fundamental,
        ser_method::use_internal_serializer,
        F,
        std::unique_ptr<T>
    > {
        template<typename Archive>
        static Archive& save(Archive& ar, const std::unique_ptr<T>& ptr) {
            T* t = ptr.get();
            if (t) {
                ar & true;
                ar & *t;
            } else {
                ar & false;
            }
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, std::unique_ptr<T>& ptr) {
            bool b=false;
            ar & b;
            if (b) {
                ptr.reset(new T());
                ar & *ptr;
            } else {
                ptr.reset();
            }
            return ar;
        }
    };

    template<std::size_t F, typename T>
    struct serializer<
        type_prop::not_a_fundamental,
        ser_method::use_internal_serializer,
        F,
        std::shared_ptr<T>
    > {
        template<typename Archive>
        static Archive& save(Archive& ar, const std::shared_ptr<T>& ptr) {
            T* t = ptr.get();
            if (t) {
                ar & true;
                ar & *t;
            }
            else {
                ar & false;
            }
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, std::shared_ptr<T>& ptr) {
            bool b = false;
            ar & b;
            if (b) {
                ptr.reset(new T());
                ar & *ptr;
            }
            else {
                ptr.reset();
            }
            return ar;
        }
    };

	/// ECC::InnerProduct
	struct InnerProductFlags
	{
		static const uint32_t N = ECC::InnerProduct::nCycles * 2;
		static const uint32_t N_Max = (N + 7) & ~7;
		uint8_t m_pF[N_Max >> 3];

		void get(uint32_t i, uint8_t& b) const
		{
			assert(i < N_Max);
			uint8_t x = m_pF[i >> 3];
			uint8_t msk = 1 << (i & 7);

			b = (0 != (x & msk));
		}
		void set(uint32_t i, uint8_t b)
		{
			// assume flags are zero-initialized
			if (b)
			{
				assert(i < N_Max);
				uint8_t& x = m_pF[i >> 3];
				uint8_t msk = 1 << (i & 7);

				x |= msk;
			}
		}

		void save(const ECC::InnerProduct& v)
		{
			uint32_t iBit = 0;
			for (size_t i = 0; i < _countof(v.m_pLR); i++)
				for (size_t j = 0; j < _countof(v.m_pLR[i]); j++)
					set(iBit++, v.m_pLR[i][j].m_Y);
		}

		void load(ECC::InnerProduct& v) const
		{
			uint32_t iBit = 0;
			for (size_t i = 0; i < _countof(v.m_pLR); i++)
				for (size_t j = 0; j < _countof(v.m_pLR[i]); j++)
					get(iBit++, v.m_pLR[i][j].m_Y);
		}
	};


    template<std::size_t F, typename T>
    struct serializer<type_prop::not_a_fundamental, ser_method::use_internal_serializer, F, T>
    {

        ///////////////////////////////////////////////////////////
        /// ECC serialization adapters
        ///////////////////////////////////////////////////////////

        /// ECC::Point serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::Point& point)
        {
            ar
                & point.m_X
                & point.m_Y;
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::Point& point)
        {
            ar
                & point.m_X
                & point.m_Y;
            return ar;
        }

        /// ECC::uintBig serialization
        template<typename Archive, uint32_t nBytes_>
        static Archive& save(Archive& ar, const beam::uintBig_t<nBytes_>& val)
        {
            ar & val.m_pData;
            return ar;
        }

        template<typename Archive, uint32_t nBytes_>
        static Archive& load(Archive& ar, beam::uintBig_t<nBytes_>& val)
        {
            ar & val.m_pData;
            return ar;
        }

		/// beam::FourCC serialization
		template<typename Archive>
		static Archive& save(Archive& ar, const beam::FourCC& val)
		{
			ar & beam::uintBigFrom(val.V);
			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::FourCC& val)
		{
			beam::uintBigFor<uint32_t>::Type x;
			ar & x;
			x.Export(val.V);
			return ar;
		}

		/// ECC::Scalar serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::Scalar& scalar)
        {
			assert(scalar.IsValid());
            ar & scalar.m_Value;
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::Scalar& scalar)
        {
            ar & scalar.m_Value;
			scalar.TestValid(); // prevent ambiguity

            return ar;
        }

		/// ECC::Key::IDV serialization
		template<typename Archive>
		static Archive& save(Archive& ar, const ECC::Key::IDV& kidv)
		{
			ar
				& kidv.m_Idx
				& kidv.m_Type
				& kidv.m_Value;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, ECC::Key::IDV& kidv)
		{
			ar
				& kidv.m_Idx
				& kidv.m_Type
				& kidv.m_Value;

			return ar;
		}

		/// ECC::Key::IDVC serialization
		template<typename Archive>
		static Archive& save(Archive& ar, const ECC::Key::IDVC& kidvc)
		{
			ar
				& Cast::Down<ECC::Key::IDV>(kidvc)
				& kidvc.m_iChild;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, ECC::Key::IDVC& kidvc)
		{
			ar
				& Cast::Down<ECC::Key::IDV>(kidvc)
				& kidvc.m_iChild;

			return ar;
		}

		/// ECC::Signature serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::Signature& val)
        {
            ar
                & val.m_NoncePub
                & val.m_k
            ;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::Signature& val)
        {
            ar
                & val.m_NoncePub
                & val.m_k
            ;

            return ar;
        }

		template<typename Archive>
		static void save_nobits(Archive& ar, const ECC::InnerProduct& v)
		{
			for (size_t i = 0; i < _countof(v.m_pLR); i++)
				for (size_t j = 0; j < _countof(v.m_pLR[i]); j++)
					ar & v.m_pLR[i][j].m_X;

			for (size_t j = 0; j < _countof(v.m_pCondensed); j++)
				ar & v.m_pCondensed[j];
		}

		template<typename Archive>
		static Archive& save(Archive& ar, const ECC::InnerProduct& v)
		{
			save_nobits(ar, v);

			InnerProductFlags ipf;
			ZeroObject(ipf);

			ipf.save(v);
			ar & ipf.m_pF;

			return ar;
		}

		template<typename Archive>
		static void load_nobits(Archive& ar, ECC::InnerProduct& v)
		{
			for (size_t i = 0; i < _countof(v.m_pLR); i++)
				for (size_t j = 0; j < _countof(v.m_pLR[i]); j++)
					ar & v.m_pLR[i][j].m_X;

			for (size_t j = 0; j < _countof(v.m_pCondensed); j++)
				ar & v.m_pCondensed[j];
		}

		template<typename Archive>
		static Archive& load(Archive& ar, ECC::InnerProduct& v)
		{
			load_nobits(ar, v);

			InnerProductFlags ipf;
			ar & ipf.m_pF;
			ipf.load(v);

			return ar;
		}

        /// ECC::RangeProof::Confidential serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::RangeProof::Confidential& v)
        {
			ar
				& v.m_Part1.m_A.m_X
				& v.m_Part1.m_S.m_X
				& v.m_Part2.m_T1.m_X
				& v.m_Part2.m_T2.m_X
				& v.m_Part3.m_TauX
				& v.m_Mu
				& v.m_tDot;

			save_nobits(ar, v.m_P_Tag);

			InnerProductFlags ipf;
			ZeroObject(ipf);

			ipf.save(v.m_P_Tag);

			static_assert(ipf.N_Max - ipf.N == 4, "");
			ipf.set(ipf.N + 0, v.m_Part1.m_A.m_Y);
			ipf.set(ipf.N + 1, v.m_Part1.m_S.m_Y);
			ipf.set(ipf.N + 2, v.m_Part2.m_T1.m_Y);
			ipf.set(ipf.N + 3, v.m_Part2.m_T2.m_Y);

			ar & ipf.m_pF;
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::RangeProof::Confidential& v)
        {
			ar
				& v.m_Part1.m_A.m_X
				& v.m_Part1.m_S.m_X
				& v.m_Part2.m_T1.m_X
				& v.m_Part2.m_T2.m_X
				& v.m_Part3.m_TauX
				& v.m_Mu
				& v.m_tDot;

			load_nobits(ar, v.m_P_Tag);

			InnerProductFlags ipf;
			ar & ipf.m_pF;

			ipf.load(v.m_P_Tag);

			static_assert(ipf.N_Max - ipf.N == 4, "");
			ipf.get(ipf.N + 0, v.m_Part1.m_A.m_Y);
			ipf.get(ipf.N + 1, v.m_Part1.m_S.m_Y);
			ipf.get(ipf.N + 2, v.m_Part2.m_T1.m_Y);
			ipf.get(ipf.N + 3, v.m_Part2.m_T2.m_Y);

			return ar;
		}

        /// ECC::RangeProof::Public serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::RangeProof::Public& val)
        {
            ar
                & val.m_Value
                & val.m_Signature
				& val.m_Kid.m_Idx
				& val.m_Kid.m_Type
            ;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::RangeProof::Public& val)
        {
            ar
                & val.m_Value
                & val.m_Signature
				& val.m_Kid.m_Idx
				& val.m_Kid.m_Type
            ;

            return ar;
        }

        ///////////////////////////////////////////////////////////
        /// Common Beam serialization adapters
        ///////////////////////////////////////////////////////////

        /// beam::Input serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::Input& input)
        {
			uint8_t nFlags =
				(input.m_Commitment.m_Y ? 1 : 0);

			ar
				& nFlags
				& input.m_Commitment.m_X;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::Input& input)
        {
			uint8_t nFlags;
			ar
				& nFlags
				& input.m_Commitment.m_X;

			input.m_Commitment.m_Y = (1 & nFlags);

            return ar;
        }

        /// beam::Output serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::Output& output)
        {
			uint8_t nFlags =
				(output.m_Commitment.m_Y ? 1 : 0) |
				(output.m_Coinbase ? 2 : 0) |
				(output.m_pConfidential ? 4 : 0) |
				(output.m_pPublic ? 8 : 0) |
				(output.m_Incubation ? 0x10 : 0);

			ar
				& nFlags
				& output.m_Commitment.m_X;

			if (output.m_pConfidential)
				ar & *output.m_pConfidential;

			if (output.m_pPublic)
				ar & *output.m_pPublic;

			if (output.m_Incubation)
				ar & output.m_Incubation;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::Output& output)
        {
			uint8_t nFlags;
			ar
				& nFlags
				& output.m_Commitment.m_X;

			output.m_Commitment.m_Y = (1 & nFlags);
			output.m_Coinbase = 0 != (2 & nFlags);

			if (4 & nFlags)
			{
				output.m_pConfidential = std::make_unique<ECC::RangeProof::Confidential>();
				ar & *output.m_pConfidential;
			}

			if (8 & nFlags)
			{
				output.m_pPublic = std::make_unique<ECC::RangeProof::Public>();
				ar & *output.m_pPublic;
			}

			if (0x10 & nFlags)
				ar & output.m_Incubation;

            return ar;
        }

		/// beam::TxKernel::HashLock serialization
		template<typename Archive>
		static Archive& save(Archive& ar, const beam::TxKernel::HashLock& val)
		{
			ar
				& val.m_Preimage
				;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::TxKernel::HashLock& val)
		{
			ar
				& val.m_Preimage
				;

			return ar;
		}

        /// beam::TxKernel serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::TxKernel& val)
        {
			uint8_t nFlags =
				(val.m_Commitment.m_Y ? 1 : 0) |
				(val.m_Fee ? 2 : 0) |
				(val.m_Height.m_Min ? 4 : 0) |
				((val.m_Height.m_Max != beam::Height(-1)) ? 8 : 0) |
				(val.m_Signature.m_NoncePub.m_Y ? 0x10 : 0) |
				(val.m_pHashLock ? 0x20 : 0) |
				(val.m_vNested.empty() ? 0 : 0x40);

			ar
				& nFlags
				& val.m_Commitment.m_X
				& val.m_Signature.m_NoncePub.m_X
				& val.m_Signature.m_k;

			if (2 & nFlags)
				ar & val.m_Fee;
			if (4 & nFlags)
				ar & val.m_Height.m_Min;
			if (8 & nFlags)
			{
				beam::Height dh = val.m_Height.m_Max - val.m_Height.m_Min;
				ar & dh;
			}
			if (0x20 & nFlags)
				ar & *val.m_pHashLock;

			if (0x40 & nFlags)
			{
				uint32_t nCount = (uint32_t) val.m_vNested.size();
				ar & nCount;

				for (uint32_t i = 0; i < nCount; i++)
					save(ar, *val.m_vNested[i]);
			}

            return ar;
        }

        template<typename Archive>
        static Archive& load_Recursive(Archive& ar, beam::TxKernel& val, uint32_t nRecusion)
        {
			uint8_t nFlags;
			ar
				& nFlags
				& val.m_Commitment.m_X
				& val.m_Signature.m_NoncePub.m_X
				& val.m_Signature.m_k;

			val.m_Commitment.m_Y = (1 & nFlags);

			if (2 & nFlags)
				ar & val.m_Fee;
			else
				val.m_Fee = 0;

			if (4 & nFlags)
				ar & val.m_Height.m_Min;
			else
				val.m_Height.m_Min = 0;

			if (8 & nFlags)
			{
				beam::Height dh;
				ar & dh;
				val.m_Height.m_Max = val.m_Height.m_Min + dh;
			}
			else
				val.m_Height.m_Max = beam::Height(-1);

			val.m_Signature.m_NoncePub.m_Y = ((0x10 & nFlags) != 0);

			if (0x20 & nFlags)
			{
				val.m_pHashLock.reset(new beam::TxKernel::HashLock);
				ar & *val.m_pHashLock;
			}

			if (0x40 & nFlags)
			{
				beam::TxKernel::TestRecursion(++nRecusion);

				uint32_t nCount;
				ar & nCount;
				val.m_vNested.resize(nCount);

				for (uint32_t i = 0; i < nCount; i++)
				{
					std::unique_ptr<beam::TxKernel>& v = val.m_vNested[i];
					v = std::make_unique<beam::TxKernel>();
					load_Recursive(ar, *v, nRecusion);
				}
			}

            return ar;
        }

		template<typename Archive>
		static Archive& load(Archive& ar, beam::TxKernel& val)
		{
			return load_Recursive(ar, val, 0);
		}

        /// beam::Transaction serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::TxBase& txb)
        {
            ar
				& txb.m_Offset;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::TxBase& txb)
        {
            ar
				& txb.m_Offset;

            return ar;
        }

		template <typename Archive, typename TPtr>
		static void save_VecPtr(Archive& ar, const std::vector<TPtr>& v)
		{
			uint32_t nSize = static_cast<uint32_t>(v.size());
			ar & beam::uintBigFrom(nSize);

			for (uint32_t i = 0; i < nSize; i++)
				ar & *v[i];
		}

		template <typename Archive, typename TPtr>
		static void load_VecPtr(Archive& ar, std::vector<TPtr>& v)
		{
			beam::uintBigFor<uint32_t>::Type x;
			ar & x;
			
			uint32_t nSize;
			x.Export(nSize);

			v.resize(nSize);
			for (uint32_t i = 0; i < nSize; i++)
			{
				v[i].reset(new typename TPtr::element_type);
				ar & *v[i];
			}
		}

        template<typename Archive>
        static Archive& save(Archive& ar, const beam::TxVectors::Perishable& txv)
        {
			save_VecPtr(ar, txv.m_vInputs);
			save_VecPtr(ar, txv.m_vOutputs);
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::TxVectors::Perishable& txv)
        {
			load_VecPtr(ar, txv.m_vInputs);
			load_VecPtr(ar, txv.m_vOutputs);
            return ar;
        }

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::TxVectors::Eternal& txv)
		{
			save_VecPtr(ar, txv.m_vKernels);
			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::TxVectors::Eternal& txv)
		{
			load_VecPtr(ar, txv.m_vKernels);
			return ar;
		}

		template<typename Archive>
        static Archive& save(Archive& ar, const beam::Transaction& tx)
        {
			ar
				& Cast::Down<beam::TxVectors::Perishable>(tx)
				& Cast::Down<beam::TxVectors::Eternal>(tx)
				& Cast::Down<beam::TxBase>(tx);

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::Transaction& tx)
        {
			ar
				& Cast::Down<beam::TxVectors::Perishable>(tx)
				& Cast::Down<beam::TxVectors::Eternal>(tx)
				& Cast::Down<beam::TxBase>(tx);

            return ar;
        }

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::Block::PoW& pow)
		{
			ar
				& pow.m_Indices
				& pow.m_Difficulty.m_Packed
				& pow.m_Nonce;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::Block::PoW& pow)
		{
			ar
				& pow.m_Indices
				& pow.m_Difficulty.m_Packed
				& pow.m_Nonce;

			return ar;
		}

		template<typename Archive>
        static Archive& save(Archive& ar, const beam::Block::SystemState::ID& v)
        {
            ar
				& v.m_Height
				& v.m_Hash;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::Block::SystemState::ID& v)
        {
			ar
				& v.m_Height
				& v.m_Hash;

            return ar;
        }

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::Block::SystemState::Sequence::Prefix& v)
		{
			ar
				& v.m_Height
				& v.m_Prev
				& v.m_ChainWork;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::Block::SystemState::Sequence::Prefix& v)
		{
			ar
				& v.m_Height
				& v.m_Prev
				& v.m_ChainWork;

			return ar;
		}

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::Block::SystemState::Sequence::Element& v)
		{
			ar
				& v.m_Kernels
				& v.m_Definition
				& v.m_TimeStamp
				& v.m_PoW;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::Block::SystemState::Sequence::Element& v)
		{
			ar
				& v.m_Kernels
				& v.m_Definition
				& v.m_TimeStamp
				& v.m_PoW;

			return ar;
		}

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::Block::SystemState::Full& v)
		{
			save(ar, Cast::Down<beam::Block::SystemState::Sequence::Prefix>(v));
			save(ar, Cast::Down<beam::Block::SystemState::Sequence::Element>(v));

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::Block::SystemState::Full& v)
		{
			load(ar, Cast::Down<beam::Block::SystemState::Sequence::Prefix>(v));
			load(ar, Cast::Down<beam::Block::SystemState::Sequence::Element>(v));

			return ar;
		}

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::Block::BodyBase& bb)
		{
			ar & Cast::Down<beam::TxBase>(bb);
			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::Block::BodyBase& bb)
		{
			ar & Cast::Down<beam::TxBase>(bb);
			return ar;
		}

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::Block::Body& bb)
		{
			ar & Cast::Down<beam::Block::BodyBase>(bb);
			ar & Cast::Down<beam::TxVectors::Perishable>(bb);
			ar & Cast::Down<beam::TxVectors::Eternal>(bb);

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::Block::Body& bb)
		{
			ar & Cast::Down<beam::Block::BodyBase>(bb);
			ar & Cast::Down<beam::TxVectors::Perishable>(bb);
			ar & Cast::Down<beam::TxVectors::Eternal>(bb);

			return ar;
		}
	};
}
}
