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
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::uintBig& val)
        {
            ar & val.m_pData;
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::uintBig& val)
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

        /// ECC::RangeProof::Confidential serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const ECC::RangeProof::Confidential& cond)
        {
            ar & cond.m_pOpaque;
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, ECC::RangeProof::Confidential& cond)
        {
            ar & cond.m_pOpaque;
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
                & input.m_Commitment
                & input.m_Coinbase
                & input.m_Height;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::Input& input)
        {
            ar
                & input.m_Commitment
                & input.m_Coinbase
                & input.m_Height;

            return ar;
        }

        /// beam::Output serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::Output& output)
        {
            ar
                & output.m_Commitment
                & output.m_Coinbase
                & output.m_pConfidential
                & output.m_pPublic;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::Output& output)
        {
            ar
                & output.m_Commitment
                & output.m_Coinbase
                & output.m_pConfidential
                & output.m_pPublic;

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
            ar
                & val.m_Excess
                & val.m_Signature
                & val.m_Fee
                & val.m_HeightMin
                & val.m_HeightMax
                & val.m_pContract
                & val.m_vNested
            ;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::TxKernel& val)
        {
            ar
                & val.m_Excess
                & val.m_Signature
                & val.m_Fee
                & val.m_HeightMin
                & val.m_HeightMax
                & val.m_pContract
                & val.m_vNested
            ;

            return ar;
        }

        /// beam::Transaction serialization
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::TxBase& tx)
        {
            ar
                & tx.m_vInputs
                & tx.m_vOutputs
                & tx.m_vKernels
                & tx.m_Offset;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::TxBase& tx)
        {
            ar
                & tx.m_vInputs
                & tx.m_vOutputs
                & tx.m_vKernels
                & tx.m_Offset;

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
        static Archive& save(Archive& ar, const beam::Block::Body& bb)
        {
            ar & (const beam::TxBase&) bb;

            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::Block::Body& bb)
        {
            ar & (beam::TxBase&) bb;

            return ar;
        }
	};
}
}
