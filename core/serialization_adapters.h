#pragma once

#include "common.h"
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
        template<typename Archive, uint32_t nBits_>
        static Archive& save(Archive& ar, const ECC::uintBig_t<nBits_>& val)
        {
            ar & val.m_pData;
            return ar;
        }

        template<typename Archive, uint32_t nBits_>
        static Archive& load(Archive& ar, ECC::uintBig_t<nBits_>& val)
        {
            ar & val.m_pData;
            return ar;
        }

        /// ECC::Scalar serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::Scalar& scalar)
        {
            ar & scalar.m_Value;
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::Scalar& scalar)
        {
            ar & scalar.m_Value;
            return ar;
        }

        /// ECC::Signature serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::Signature& val)
        {
            ar
                & val.m_e
                & val.m_k
            ;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::Signature& val)
        {
            ar
                & val.m_e
                & val.m_k
            ;

            return ar;
        }

		/// ECC::InnerProduct
		template<typename Archive>
		static Archive& save(Archive& ar, const ECC::InnerProduct& v)
		{
			ar
				& v.m_AB
				& v.m_pLR
				& v.m_pCondensed;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, ECC::InnerProduct& v)
		{
			ar
				& v.m_AB
				& v.m_pLR
				& v.m_pCondensed;

			return ar;
		}

        /// ECC::RangeProof::Confidential serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::RangeProof::Confidential& v)
        {
			ar
				& v.m_A
				& v.m_S
				& v.m_T1
				& v.m_T2
				& v.m_TauX
				& v.m_Mu
				& v.m_tDot
				& v.m_P_Tag;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::RangeProof::Confidential& v)
        {
			ar
				& v.m_A
				& v.m_S
				& v.m_T1
				& v.m_T2
				& v.m_TauX
				& v.m_Mu
				& v.m_tDot
				& v.m_P_Tag;

			return ar;
        }

        /// ECC::RangeProof::Public serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::RangeProof::Public& val)
        {
            ar
                & val.m_Value
                & val.m_Signature
            ;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::RangeProof::Public& val)
        {
            ar
                & val.m_Value
                & val.m_Signature
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
			ar
				& input.m_Commitment;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::Input& input)
        {
			ar
				& input.m_Commitment;

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
				(output.m_Incubation ? 0x10 : 0) |
				(output.m_hDelta ? 0x20 : 0);

			ar
				& nFlags
				& output.m_Commitment.m_X;

			if (output.m_pConfidential)
				ar & output.m_pConfidential;

			if (output.m_pPublic)
				ar & output.m_pPublic;

			if (output.m_Incubation)
				ar & output.m_Incubation;

			if (output.m_hDelta)
				ar & output.m_hDelta;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::Output& output)
        {
			uint8_t nFlags;
			ar
				& nFlags
				& output.m_Commitment.m_X;

			output.m_Commitment.m_Y = 0 != (1 & nFlags);
			output.m_Coinbase = 0 != (2 & nFlags);

			if (4 & nFlags)
				ar & output.m_pConfidential;

			if (8 & nFlags)
				ar & output.m_pPublic;

			if (0x10 & nFlags)
				ar & output.m_Incubation;

			if (0x20 & nFlags)
				ar & output.m_hDelta;

            return ar;
        }

        /// beam::TxKernel::Contract serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::TxKernel::Contract& val)
        {
            ar
                & val.m_Msg
                & val.m_PublicKey
                & val.m_Signature
            ;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::TxKernel::Contract& val)
        {
            ar
                & val.m_Msg
                & val.m_PublicKey
                & val.m_Signature
            ;

            return ar;
        }

        /// beam::TxKernel serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::TxKernel& val)
        {
			uint8_t nFlags =
				(val.m_Multiplier ? 1 : 0) |
				(val.m_Fee ? 2 : 0) |
				(val.m_HeightMin ? 4 : 0) |
				((val.m_HeightMax != beam::Height(-1)) ? 8 : 0) |
				(val.m_pContract ? 0x10 : 0) |
				(val.m_vNested.empty() ? 0 : 0x20);

			ar
				& nFlags
				& val.m_Excess
				& val.m_Signature;

			if (1 & nFlags)
				ar & val.m_Multiplier;
			if (2 & nFlags)
				ar & val.m_Fee;
			if (4 & nFlags)
				ar & val.m_HeightMin;
			if (8 & nFlags)
				ar & val.m_HeightMax;
			if (0x10 & nFlags)
				ar & val.m_pContract;
			if (0x20 & nFlags)
				ar & val.m_vNested;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::TxKernel& val)
        {
			uint8_t nFlags;
			ar
				& nFlags
				& val.m_Excess
				& val.m_Signature;

			if (1 & nFlags)
				ar & val.m_Multiplier;
			else
				val.m_Multiplier = 0;

			if (2 & nFlags)
				ar & val.m_Fee;
			else
				val.m_Fee = 0;

			if (4 & nFlags)
				ar & val.m_HeightMin;
			else
				val.m_HeightMin = 0;

			if (8 & nFlags)
				ar & val.m_HeightMax;
			else
				val.m_HeightMax = beam::Height(-1);

			if (0x10 & nFlags)
				ar & val.m_pContract;

			if (0x20 & nFlags)
				ar & val.m_vNested;

            return ar;
        }

        /// beam::Transaction serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::TxBase& tx)
        {
            ar
                & tx.m_vInputs
                & tx.m_vOutputs
                & tx.m_vKernelsInput
				& tx.m_vKernelsOutput
				& tx.m_Offset;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::TxBase& tx)
        {
            ar
                & tx.m_vInputs
                & tx.m_vOutputs
				& tx.m_vKernelsInput
				& tx.m_vKernelsOutput
				& tx.m_Offset;

			tx.TestNoNulls();

            return ar;
        }

		template<typename Archive>
        static Archive& save(Archive& ar, const beam::Transaction& tx)
        {
            ar & (const beam::TxBase&) tx;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::Transaction& tx)
        {
            ar & (beam::TxBase&) tx;

            return ar;
        }

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::Block::PoW& pow)
		{
			ar
				& pow.m_Indices
				& pow.m_Difficulty
				& pow.m_Nonce;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::Block::PoW& pow)
		{
			ar
				& pow.m_Indices
				& pow.m_Difficulty
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
		static Archive& save(Archive& ar, const beam::Block::SystemState::Full& v)
		{
			ar
				& v.m_Height
				& v.m_Prev
				& v.m_Definition
				& v.m_TimeStamp
				& v.m_PoW;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::Block::SystemState::Full& v)
		{
			ar
				& v.m_Height
				& v.m_Prev
				& v.m_Definition
				& v.m_TimeStamp
				& v.m_PoW;

			return ar;
		}

		template<typename Archive>
		static Archive& save(Archive& ar, const beam::Block::Body& bb)
		{
			uint8_t nFlags =
				(bb.m_Subsidy.Hi ? 1 : 0) |
				(bb.m_SubsidyClosing ? 2 : 0);

			ar & (const beam::TxBase&) bb;
			ar & nFlags;
			ar & bb.m_Subsidy.Lo;

			if (bb.m_Subsidy.Hi)
				ar & bb.m_Subsidy.Hi;

			return ar;
		}

		template<typename Archive>
		static Archive& load(Archive& ar, beam::Block::Body& bb)
		{
			uint8_t nFlags;

			ar & (beam::TxBase&) bb;
			ar & nFlags;
			ar & bb.m_Subsidy.Lo;

			if (1 & nFlags)
				ar & bb.m_Subsidy.Hi;
			else
				bb.m_Subsidy.Hi = 0;

			bb.m_SubsidyClosing = ((2 & nFlags) != 0);

			return ar;
		}
	};
}
}
