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
#include "utility/serialize.h"

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
		static_assert(!std::is_base_of<beam::TxKernel, T>::value);

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

	/// Multibit, many bits packed
	template <uint32_t nBits>
	struct Multibit
	{
		static const uint32_t N = nBits;
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
	};


	/// ECC::InnerProduct
	struct InnerProductFlags
		:public Multibit<ECC::InnerProduct::nCycles * 2>
	{
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

		/// ECC::Point::Storage serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::Point::Storage& point)
        {
            ar
                & point.m_X
                & point.m_Y;
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::Point::Storage& point)
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
				& kidv.m_SubIdx
				& kidv.m_Value;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, ECC::Key::IDV& kidv)
		{
			ar
				& kidv.m_Idx
				& kidv.m_Type
				& kidv.m_SubIdx
				& kidv.m_Value;

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

		/// ECC::Signature serialization
        template<typename Archive, uint32_t nG>
        static Archive& save(Archive& ar, const ECC::SignatureGeneralized<nG>& val)
        {
            ar & val.m_NoncePub;

			for (uint32_t i = 0; i < nG; i++)
                ar & val.m_pK[i];

            return ar;
        }

        template<typename Archive, uint32_t nG>
        static Archive& load(Archive& ar, ECC::SignatureGeneralized<nG>& val)
        {
			ar& val.m_NoncePub;

			for (uint32_t i = 0; i < nG; i++)
				ar& val.m_pK[i];

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
        static Archive& save(Archive& ar, const ECC::RangeProof::Confidential& v, bool bRecoveryOnly = false)
        {
			ar
				& v.m_Part1.m_A.m_X
				& v.m_Part1.m_S.m_X
				& v.m_Part2.m_T1.m_X
				& v.m_Part2.m_T2.m_X;

			if (bRecoveryOnly)
			{
				uint8_t nFlags =
					(v.m_Part1.m_A.m_Y ? 1 : 0) |
					(v.m_Part1.m_S.m_Y ? 2 : 0) |
					(v.m_Part2.m_T1.m_Y ? 4 : 0) |
					(v.m_Part2.m_T2.m_Y ? 8 : 0);

				ar
					& v.m_Mu
					& nFlags;
			}
			else
			{
				ar
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
			}
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::RangeProof::Confidential& v, bool bRecoveryOnly = false)
        {
			ar
				& v.m_Part1.m_A.m_X
				& v.m_Part1.m_S.m_X
				& v.m_Part2.m_T1.m_X
				& v.m_Part2.m_T2.m_X;

			if (bRecoveryOnly)
			{
				uint8_t nFlags;

				ar
					& v.m_Mu
					& nFlags;

				v.m_Part1.m_A.m_Y = 0 != (1 & nFlags);
				v.m_Part1.m_S.m_Y = 0 != (2 & nFlags);
				v.m_Part2.m_T1.m_Y = 0 != (4 & nFlags);
				v.m_Part2.m_T2.m_Y = 0 != (8 & nFlags);

				ZeroObject(v.m_Part3);
				ZeroObject(v.m_tDot);
				ZeroObject(v.m_P_Tag);
			}
			else
			{
				ar
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
			}
			return ar;
		}

        /// ECC::RangeProof::Confidential::Part2
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::RangeProof::Confidential::Part2& v)
        {
            ar
                & v.m_T1
                & v.m_T2;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::RangeProof::Confidential::Part2& v)
        {
            ar
                & v.m_T1
                & v.m_T2;

            return ar;
        }

        /// ECC::RangeProof::Confidential::Part3
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::RangeProof::Confidential::Part3& v)
        {
            ar
                & v.m_TauX;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::RangeProof::Confidential::Part3& v)
        {
            ar
                & v.m_TauX;

            return ar;
        }

        /// ECC::RangeProof::Confidential::MultiSig
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::RangeProof::Confidential::MultiSig& v)
        {
			ar
				& v.m_Part1.m_A
				& v.m_Part1.m_S
				& v.m_Part2.m_T1
				& v.m_Part2.m_T2;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::RangeProof::Confidential::MultiSig& v)
        {
			ar
				& v.m_Part1.m_A
				& v.m_Part1.m_S
				& v.m_Part2.m_T1
				& v.m_Part2.m_T2;

            return ar;
        }

        /// ECC::RangeProof::Public serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::RangeProof::Public& val, bool bRecoveryOnly = false)
        {
			ar & val.m_Value;

			if (!bRecoveryOnly)
			{
				ar & val.m_Signature;
			}

            ar
				& val.m_Recovery.m_Kid.m_Idx
				& val.m_Recovery.m_Kid.m_Type
				& val.m_Recovery.m_Kid.m_SubIdx
				& val.m_Recovery.m_Checksum
				;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::RangeProof::Public& val, bool bRecoveryOnly = false)
        {
			ar & val.m_Value;

			if (bRecoveryOnly)
			{
				ZeroObject(val.m_Signature);
			}
			else
			{
				ar & val.m_Signature;
			}

			ar
				& val.m_Recovery.m_Kid.m_Idx
				& val.m_Recovery.m_Kid.m_Type
				& val.m_Recovery.m_Kid.m_SubIdx
				& val.m_Recovery.m_Checksum
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

		template<typename Archive>
		class MultibitVar
		{
			Archive& m_ar;
			uint8_t m_Flag = 0;
			uint8_t m_Bits = 0;

		public:
			MultibitVar(Archive& ar) :m_ar(ar) {}

			void put(uint8_t i)
			{
				assert(i <= 1);
				m_Flag |= (i << (7 - m_Bits));

				if (++m_Bits == 8)
				{
					m_ar & m_Flag;
					m_Bits = 0;
					m_Flag = 0;
				}
			}

			void Flush()
			{
				if (m_Bits)
					m_ar & m_Flag;
			}

			void get(uint8_t& res)
			{
				if (!m_Bits)
				{
					m_ar & m_Flag;
					m_Bits = 8;
				}

				res = 1 & (m_Flag >> (--m_Bits));
			}
		};

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::Lelantus::Proof& v)
		{
			ar
				& v.m_Cfg.n
				& v.m_Cfg.M
				& v.m_Part1.m_Commitment.m_X
				& v.m_Part1.m_SpendPk.m_X
				& v.m_Part1.m_A.m_X
				& v.m_Part1.m_B.m_X
				& v.m_Part1.m_C.m_X
				& v.m_Part1.m_D.m_X
				& v.m_Part1.m_Nonce.m_X
				& v.m_Part2.m_zA
				& v.m_Part2.m_zC
				& v.m_Part2.m_zR
				& v.m_Part2.m_ProofG
				& v.m_Part2.m_ProofH;

			assert(v.m_Part1.m_vG.size() >= v.m_Cfg.M);
			for (uint32_t i = 0; i < v.m_Cfg.M; i++)
				ar & v.m_Part1.m_vG[i].m_X;

			MultibitVar<Archive> mb(ar);

			mb.put(v.m_Part1.m_Commitment.m_Y);
			mb.put(v.m_Part1.m_SpendPk.m_Y);
			mb.put(v.m_Part1.m_A.m_Y);
			mb.put(v.m_Part1.m_B.m_Y);
			mb.put(v.m_Part1.m_C.m_Y);
			mb.put(v.m_Part1.m_D.m_Y);
			mb.put(v.m_Part1.m_Nonce.m_Y);

			for (uint32_t i = 0; i < v.m_Cfg.M; i++)
				mb.put(v.m_Part1.m_vG[i].m_Y);

			mb.Flush();

			uint32_t nSizeF = v.m_Cfg.get_F();
			assert(v.m_Part2.m_vF.size() >= nSizeF);

			for (uint32_t i = 0; i < nSizeF; i++)
				ar & v.m_Part2.m_vF[i];

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::Lelantus::Proof& v)
		{
			ar
				& v.m_Cfg.n
				& v.m_Cfg.M
				& v.m_Part1.m_Commitment.m_X
				& v.m_Part1.m_SpendPk.m_X
				& v.m_Part1.m_A.m_X
				& v.m_Part1.m_B.m_X
				& v.m_Part1.m_C.m_X
				& v.m_Part1.m_D.m_X
				& v.m_Part1.m_Nonce.m_X
				& v.m_Part2.m_zA
				& v.m_Part2.m_zC
				& v.m_Part2.m_zR
				& v.m_Part2.m_ProofG
				& v.m_Part2.m_ProofH;

			if (!v.m_Cfg.get_N())
				throw std::runtime_error("L/Cfg");

			v.m_Part1.m_vG.resize(v.m_Cfg.M);
			for (uint32_t i = 0; i < v.m_Cfg.M; i++)
				ar & v.m_Part1.m_vG[i].m_X;

			MultibitVar<Archive> mb(ar);

			mb.get(v.m_Part1.m_Commitment.m_Y);
			mb.get(v.m_Part1.m_SpendPk.m_Y);
			mb.get(v.m_Part1.m_A.m_Y);
			mb.get(v.m_Part1.m_B.m_Y);
			mb.get(v.m_Part1.m_C.m_Y);
			mb.get(v.m_Part1.m_D.m_Y);
			mb.get(v.m_Part1.m_Nonce.m_Y);

			for (uint32_t i = 0; i < v.m_Cfg.M; i++)
				mb.get(v.m_Part1.m_vG[i].m_Y);

			uint32_t nSizeF = v.m_Cfg.get_F();
			v.m_Part2.m_vF.resize(nSizeF);

			for (uint32_t i = 0; i < nSizeF; i++)
				ar & v.m_Part2.m_vF[i];

			return ar;
		}

		/// beam::ShieldedTxo::Serial serialization
		template<typename Archive>
		static Archive& save(Archive& ar, const beam::ShieldedTxo::Serial& x)
		{
			uint8_t nFlags =
				(x.m_SerialPub.m_Y ? 1 : 0) |
				(x.m_Signature.m_NoncePub.m_Y ? 2 : 0);

			ar
				& nFlags
				& x.m_SerialPub.m_X
				& x.m_Signature.m_NoncePub.m_X
				& x.m_Signature.m_pK[0]
				& x.m_Signature.m_pK[1];

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::ShieldedTxo::Serial& x)
		{
			uint8_t nFlags;

			ar
				& nFlags
				& x.m_SerialPub.m_X
				& x.m_Signature.m_NoncePub.m_X
				& x.m_Signature.m_pK[0]
				& x.m_Signature.m_pK[1];

			x.m_SerialPub.m_Y = (1 & nFlags);
			x.m_Signature.m_NoncePub.m_Y = 0 != (2 & nFlags);

			return ar;
		}

		/// beam::ShieldedTxo serialization
		template<typename Archive>
        static Archive& save(Archive& ar, const beam::ShieldedTxo& val)
        {
			uint32_t nFlags =
				(val.m_Commitment.m_Y ? 1 : 0) |
				(val.m_Serial.m_SerialPub.m_Y ? 2 : 0) |
				(val.m_Serial.m_Signature.m_NoncePub.m_Y ? 4 : 0);

			ar
				& nFlags
				& val.m_Commitment.m_X
				& val.m_RangeProof
				& val.m_Serial.m_SerialPub.m_X
				& val.m_Serial.m_Signature.m_NoncePub
				& val.m_Serial.m_Signature.m_pK[0]
				& val.m_Serial.m_Signature.m_pK[1];

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::ShieldedTxo& val)
        {
			uint32_t nFlags;
			ar
				& nFlags
				& val.m_Commitment.m_X
				& val.m_RangeProof
				& val.m_Serial.m_SerialPub.m_X
				& val.m_Serial.m_Signature.m_NoncePub
				& val.m_Serial.m_Signature.m_pK[0]
				& val.m_Serial.m_Signature.m_pK[1];

			val.m_Commitment.m_Y = (1 & nFlags);
			val.m_Serial.m_SerialPub.m_Y = ((2 & nFlags) != 0);
			val.m_Serial.m_Signature.m_NoncePub.m_Y = ((4 & nFlags) != 0);

			return ar;
		}

        /// beam::Output serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::Output& output)
        {
			uint8_t nFlags2 = 0;

			uint8_t nFlags =
				(output.m_Commitment.m_Y ? 1 : 0) |
				(output.m_Coinbase ? 2 : 0) |
				(output.m_pConfidential ? 4 : 0) |
				(output.m_pPublic ? 8 : 0) |
				(output.m_Incubation ? 0x10 : 0) |
				((output.m_AssetID == beam::Zero) ? 0 : 0x20) |
				(output.m_RecoveryOnly ? 0x40 : 0) |
				(nFlags2 ? 0x80 : 0);

			ar
				& nFlags
				& output.m_Commitment.m_X;

			if (output.m_pConfidential)
				save(ar, *output.m_pConfidential, output.m_RecoveryOnly);

			if (output.m_pPublic)
				save(ar, *output.m_pPublic, output.m_RecoveryOnly);

			if (output.m_Incubation)
				ar & output.m_Incubation;

			if (0x20 & nFlags)
				ar & output.m_AssetID;

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
			output.m_RecoveryOnly = 0 != (0x40 & nFlags);

			if (4 & nFlags)
			{
				output.m_pConfidential = std::make_unique<ECC::RangeProof::Confidential>();
				load(ar, *output.m_pConfidential, output.m_RecoveryOnly);
			}

			if (8 & nFlags)
			{
				output.m_pPublic = std::make_unique<ECC::RangeProof::Public>();
				load(ar, *output.m_pPublic, output.m_RecoveryOnly);
			}

			if (0x10 & nFlags)
				ar & output.m_Incubation;

			if (0x20 & nFlags)
				ar & output.m_AssetID;
			else
				output.m_AssetID = beam::Zero;

			if (0x80 & nFlags)
			{
				uint8_t nFlags2;
				ar & nFlags2;
			}

            return ar;
        }

		/// beam::TxKernelStd::HashLock serialization
		template<typename Archive>
		static Archive& save(Archive& ar, const beam::TxKernelStd::HashLock& val)
		{
			ar
				& val.m_Value
				;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::TxKernelStd::HashLock& val)
		{
			ar
				& val.m_Value
				;

			return ar;
		}

		/// beam::TxKernelStd::RelativeLock serialization
		template<typename Archive>
		static Archive& save(Archive& ar, const beam::TxKernelStd::RelativeLock& val)
		{
			ar
				& val.m_ID
				& val.m_LockHeight
				;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::TxKernelStd::RelativeLock& val)
		{
			ar
				& val.m_ID
				& val.m_LockHeight
				;

			return ar;
		}

		// TxKernel management
		struct ImplTxKernel
		{
			static void ThrowUnknownSubtype(uint8_t)
			{
				throw std::runtime_error("Bad kernel subtype");
			}

			template <typename Archive>
			static void save1(Archive& ar, const beam::TxKernel& krn, uint8_t nType)
			{
				assert(krn.get_Subtype() == nType);

				switch (nType)
				{
#define THE_MACRO(id, name) \
				case beam::TxKernel::Subtype::name: \
					ar & Cast::Up<beam::TxKernel##name>(krn); \
					break;

				BeamKernelsAll(THE_MACRO)
#undef THE_MACRO

				default:
					assert(false); // must not happen!
					ThrowUnknownSubtype(nType);
				}
			}

			template <typename Archive>
			static void save2(Archive& ar, const beam::TxKernel& krn, bool bAssumeStd)
			{
				uint8_t nType;
				if (bAssumeStd)
				{
					assert(beam::TxKernel::Subtype::Std == krn.get_Subtype());
					nType = beam::TxKernel::Subtype::Std;
				}
				else
				{
					nType = static_cast<uint8_t>(krn.get_Subtype());
					ar & nType;
				}

				save1(ar, krn, nType);
			}


			template<typename Archive>
			static Archive& load2(Archive& ar, beam::TxKernel::Ptr& pPtr, uint32_t nRecursion, bool bIsStd)
			{
				uint8_t nType;
				if (bIsStd)
					nType = beam::TxKernel::Subtype::Std;
				else
					ar & nType;

				load1(ar, pPtr, nType, nRecursion);
				pPtr->UpdateID();

				return ar;
			}


			template<typename Archive>
			static void load1(Archive& ar, beam::TxKernel::Ptr& pPtr, uint8_t nType, uint32_t nRecursion)
			{
				beam::TxKernel::TestRecursion(nRecursion);

				switch (nType)
				{
#define THE_MACRO(id, name) \
				case beam::TxKernel::Subtype::name: \
					pPtr.reset(new beam::TxKernel##name); \
					load0(ar, Cast::Up<beam::TxKernel##name>(*pPtr), nRecursion); \
					break;

					BeamKernelsAll(THE_MACRO)
#undef THE_MACRO

				default:
					ThrowUnknownSubtype(nType);
				}
			}

			static bool HasNonStd(const std::vector<beam::TxKernel::Ptr>& vec)
			{
				for (size_t i = 0; i < vec.size(); i++)
					if (beam::TxKernel::Subtype::Std != vec[i]->get_Subtype())
						return true;
				return false;
			}

			static uint8_t get_CommonFlags(const beam::TxKernel& krn)
			{
				return
					(krn.m_Fee ? 2 : 0) |
					(krn.m_Height.m_Min ? 4 : 0) |
					((krn.m_Height.m_Max != beam::Height(-1)) ? 8 : 0) |
					(krn.m_vNested.empty() ? 0 : 0x40);
			}

			template <typename Archive>
			static void save_Nested(Archive& ar, const beam::TxKernel& krn)
			{
				if (krn.m_vNested.empty())
					return;

				uint32_t nCount = 0;

				bool bNestedNonStd = ImplTxKernel::HasNonStd(krn.m_vNested);
				if (bNestedNonStd)
					ar & nCount;

				nCount = (uint32_t) krn.m_vNested.size();
				ar & nCount;

				for (uint32_t i = 0; i < nCount; i++)
					save2(ar, *krn.m_vNested[i], !bNestedNonStd);
			}

			template <typename Archive>
			static void load_Nested(Archive& ar, beam::TxKernel& krn, uint32_t nFlags, uint32_t nRecursion)
			{
				if (!(0x40 & nFlags))
					return;

				nRecursion++;

				uint32_t nCount;
				ar & nCount;

				bool bNestedNonStd = !nCount;
				if (bNestedNonStd)
					ar & nCount;

				krn.m_vNested.resize(nCount);

				for (uint32_t i = 0; i < nCount; i++)
					ImplTxKernel::load2(ar, krn.m_vNested[i], nRecursion, !bNestedNonStd);
			}

			template <typename Archive>
			static void save_FeeHeight(Archive& ar, const beam::TxKernel& krn, uint32_t nFlags)
			{
				if (2 & nFlags)
					ar & krn.m_Fee;
				if (4 & nFlags)
					ar & krn.m_Height.m_Min;
				if (8 & nFlags)
				{
					beam::Height dh = krn.m_Height.m_Max - krn.m_Height.m_Min;
					ar & dh;
				}
			}

			template <typename Archive>
			static void load_FeeHeight(Archive& ar, beam::TxKernel& krn, uint32_t nFlags)
			{
				if (2 & nFlags)
					ar & krn.m_Fee;
				else
					krn.m_Fee = 0;

				if (4 & nFlags)
					ar & krn.m_Height.m_Min;
				else
					krn.m_Height.m_Min = 0;

				if (8 & nFlags)
				{
					beam::Height dh;
					ar & dh;
					krn.m_Height.m_Max = krn.m_Height.m_Min + dh;
				}
				else
					krn.m_Height.m_Max = beam::Height(-1);
			}
		};

        /// beam::TxKernelStd serialization
		template<typename Archive>
        static Archive& save(Archive& ar, const beam::TxKernelStd& val)
        {
			uint8_t nFlags2 =
				//(val.m_AssetEmission ? 1 : 0) |
				(val.m_pRelativeLock ? 2 : 0) |
				(val.m_CanEmbed ? 4 : 0);

			uint8_t nFlags =
				ImplTxKernel::get_CommonFlags(val) |
				(val.m_Commitment.m_Y ? 1 : 0) |
				(val.m_Signature.m_NoncePub.m_Y ? 0x10 : 0) |
				(val.m_pHashLock ? 0x20 : 0) |
				(nFlags2 ? 0x80 : 0);

			ar
				& nFlags
				& val.m_Commitment.m_X
				& val.m_Signature.m_NoncePub.m_X
				& val.m_Signature.m_k;

			ImplTxKernel::save_FeeHeight(ar, val, nFlags);

			if (0x20 & nFlags)
				ar & *val.m_pHashLock;

			ImplTxKernel::save_Nested(ar, val);

			if (nFlags2)
			{
				ar & nFlags2;

				if (2 & nFlags2)
					ar & *val.m_pRelativeLock;
			}
            return ar;
        }

        template<typename Archive>
        static void load0(Archive& ar, beam::TxKernelStd& val, uint32_t nRecursion)
        {
			uint8_t nFlags;
			ar
				& nFlags
				& val.m_Commitment.m_X
				& val.m_Signature.m_NoncePub.m_X
				& val.m_Signature.m_k;

			val.m_Commitment.m_Y = (1 & nFlags);
			val.m_Signature.m_NoncePub.m_Y = ((0x10 & nFlags) != 0);

			ImplTxKernel::load_FeeHeight(ar, val, nFlags);

			if (0x20 & nFlags)
			{
				val.m_pHashLock.reset(new beam::TxKernelStd::HashLock);
				ar & *val.m_pHashLock;
			}

			ImplTxKernel::load_Nested(ar, val, nFlags, nRecursion);

			if (0x80 & nFlags)
			{
				uint8_t nFlags2;
				ar & nFlags2;

				if (2 & nFlags2)
				{
					val.m_pRelativeLock.reset(new beam::TxKernelStd::RelativeLock);
					ar & *val.m_pRelativeLock;
				}

				if (4 & nFlags2)
					val.m_CanEmbed = true;
			}
        }


        /// beam::TxKernelAssetEmit serialization
		template<typename Archive>
        static Archive& save(Archive& ar, const beam::TxKernelAssetEmit& val)
        {
			uint32_t nFlags =
				ImplTxKernel::get_CommonFlags(val) |
				(val.m_Commitment.m_Y ? 1 : 0) |
				(val.m_Signature.m_NoncePub.m_Y ? 0x10 : 0) |
				(val.m_CanEmbed ? 0x20 : 0);

			ar
				& nFlags
				& val.m_Commitment.m_X
				& val.m_Signature.m_NoncePub.m_X
				& val.m_Signature.m_pK[0]
				& val.m_AssetID
				& val.m_Value;

			ImplTxKernel::save_FeeHeight(ar, val, nFlags);
			ImplTxKernel::save_Nested(ar, val);

            return ar;
        }

        template<typename Archive>
        static void load0(Archive& ar, beam::TxKernelAssetEmit& val, uint32_t nRecursion)
        {
			uint32_t nFlags;
			ar
				& nFlags
				& val.m_Commitment.m_X
				& val.m_Signature.m_NoncePub.m_X
				& val.m_Signature.m_pK[0]
				& val.m_AssetID
				& val.m_Value;

			ImplTxKernel::load_FeeHeight(ar, val, nFlags);
			ImplTxKernel::load_Nested(ar, val, nFlags, nRecursion);

			val.m_Commitment.m_Y = (1 & nFlags);
			val.m_Signature.m_NoncePub.m_Y = ((0x10 & nFlags) != 0);

			if (0x20 & nFlags)
				val.m_CanEmbed = true;
        }

        /// beam::TxKernelShieldedOutput serialization
		template<typename Archive>
        static Archive& save(Archive& ar, const beam::TxKernelShieldedOutput& val)
        {
			uint32_t nFlags =
				ImplTxKernel::get_CommonFlags(val) |
				(val.m_CanEmbed ? 0x80 : 0);

			ar
				& nFlags
				& val.m_Txo;

			ImplTxKernel::save_FeeHeight(ar, val, nFlags);
			ImplTxKernel::save_Nested(ar, val);

            return ar;
        }

        template<typename Archive>
        static void load0(Archive& ar, beam::TxKernelShieldedOutput& val, uint32_t nRecursion)
        {
			uint32_t nFlags;
			ar
				& nFlags
				& val.m_Txo;

			ImplTxKernel::load_FeeHeight(ar, val, nFlags);
			ImplTxKernel::load_Nested(ar, val, nFlags, nRecursion);

			if (0x80 & nFlags)
				val.m_CanEmbed = true;
        }

		/// beam::TxKernelShieldedInput serialization
		template<typename Archive>
		static Archive& save(Archive& ar, const beam::TxKernelShieldedInput& val)
		{
			uint32_t nFlags =
				ImplTxKernel::get_CommonFlags(val) |
				(val.m_CanEmbed ? 0x80 : 0);

			ar
				& nFlags
				& val.m_WindowEnd
				& val.m_SpendProof;

			ImplTxKernel::save_FeeHeight(ar, val, nFlags);
			ImplTxKernel::save_Nested(ar, val);

			return ar;
		}

		template<typename Archive>
		static void load0(Archive& ar, beam::TxKernelShieldedInput& val, uint32_t nRecursion)
		{
			uint32_t nFlags;
			ar
				& nFlags
				& val.m_WindowEnd
				& val.m_SpendProof;

			ImplTxKernel::load_FeeHeight(ar, val, nFlags);
			ImplTxKernel::load_Nested(ar, val, nFlags, nRecursion);

			if (0x80 & nFlags)
				val.m_CanEmbed = true;
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

			for (size_t i = 0; i < v.size(); i++)
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
			uint32_t nSize = static_cast<uint32_t>(txv.m_vKernels.size());
			bool bStd = !ImplTxKernel::HasNonStd(txv.m_vKernels);
			if (!bStd)
			{
				const uint32_t nFlag = uint32_t(1) << 31;
				nSize |= nFlag;
			}

			ar & beam::uintBigFrom(nSize);

			for (size_t i = 0; i < txv.m_vKernels.size(); i++)
				ImplTxKernel::save2(ar, *txv.m_vKernels[i], bStd);

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::TxVectors::Eternal& txv)
		{
			beam::uintBigFor<uint32_t>::Type x;
			ar & x;

			uint32_t nSize;
			x.Export(nSize);

			const uint32_t nFlag = uint32_t(1) << 31;

			bool bStd = !(nSize & nFlag);
			if (!bStd)
				nSize &= ~nFlag;

			txv.m_vKernels.resize(nSize);

			for (size_t i = 0; i < txv.m_vKernels.size(); i++)
				ImplTxKernel::load2(ar, txv.m_vKernels[i], 0, bStd);

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

    template <std::size_t F>
    struct serializer<
        type_prop::not_a_fundamental,
        ser_method::use_internal_serializer,
        F,
        beam::TxKernel::Ptr
    > {
		template<typename Archive>
		static Archive& save(Archive& ar, const beam::TxKernel* p)
		{
			uint8_t nType = p ? static_cast<uint8_t>(p->get_Subtype()) : 0;
			ar & nType;

			if (p)
				serializer<type_prop::not_a_fundamental, ser_method::use_internal_serializer, F, beam::TxKernel>::ImplTxKernel::save1(ar, *p, nType);

			return ar;
		}

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::TxKernel::Ptr& pPtr)
		{
			return save(ar, pPtr.get());
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::TxKernel::Ptr& pPtr)
		{
			uint8_t nType;
			ar & nType;

			if (nType)
			{
				serializer<type_prop::not_a_fundamental, ser_method::use_internal_serializer, F, beam::TxKernel>::ImplTxKernel::load1(ar, pPtr, nType, 0);
				pPtr->UpdateID();
			}
			else
				pPtr.reset();

			return ar;
		}
    };

	template <std::size_t F, typename TKrn>
	struct serializerKrn
	{
		template<typename Archive>
		static Archive & save(Archive & ar, const typename TKrn::Ptr & pPtr)
		{
			return serializer<type_prop::not_a_fundamental, ser_method::use_internal_serializer, F, beam::TxKernel::Ptr>::save(ar, pPtr.get());
		}

		template<typename Archive>
		static Archive& load(Archive& ar, typename TKrn::Ptr& pPtr, beam::TxKernel::Subtype::Enum eType)
		{
			beam::TxKernel::Ptr pKrn;
			ar & pKrn;

			if (pKrn && (pKrn->get_Subtype() == eType))
				pPtr.reset(Cast::Up<TKrn>(pKrn.release()));
			else
				pPtr.reset();

			return ar;
		}
	};

#define THE_MACRO(id, name) \
    template<std::size_t F> \
    struct serializer<type_prop::not_a_fundamental, ser_method::use_internal_serializer, F, beam::TxKernel##name::Ptr> \
	{ \
        template<typename Archive> \
        static Archive& save(Archive& ar, const beam::TxKernel##name::Ptr& pPtr) \
		{ \
			return serializerKrn<F, beam::TxKernel##name>::save(ar, pPtr); \
        } \
 \
        template<typename Archive> \
        static Archive& load(Archive& ar, beam::TxKernel##name::Ptr& pPtr) \
		{ \
			return serializerKrn<F, beam::TxKernel##name>::load(ar, pPtr, beam::TxKernel::Subtype::name); \
		} \
    };

	BeamKernelsAll(THE_MACRO)
#undef THE_MACRO


	template <typename Archive>
	void SaveKrn(Archive& ar, const beam::TxKernel& krn, bool bAssumeStd)
	{
		serializer<type_prop::not_a_fundamental, ser_method::use_internal_serializer, 0, beam::TxKernel>::ImplTxKernel::save2(ar, krn, bAssumeStd);
	}

	template <typename Trg>
	struct SerializerProxy
	{
		struct Impl
		{
			Trg& m_Trg;
			Impl(Trg& trg) :m_Trg(trg) {}

			size_t write(const void* p, const size_t size)
			{
				m_Trg << beam::Blob(p, static_cast<uint32_t>(size));
				return size;
			}

		} m_Impl;

		yas::binary_oarchive<Impl, beam::SERIALIZE_OPTIONS> _oa;


		SerializerProxy(Trg& trg)
			:m_Impl(trg)
			,_oa(m_Impl)
		{
		}

		template <typename T> SerializerProxy& operator & (const T& object)
		{
			_oa & object;
			return *this;
		}
	};

}
}

template <typename T>
inline ECC::Hash::Processor& ECC::Hash::Processor::Serialize(const T& t)
{
	yas::detail::SerializerProxy<ECC::Hash::Processor>(*this) & t;
	return *this;
}
