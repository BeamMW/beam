#pragma once

#include "core/ecc_native.h"
#include "wallet/common.h"
#include "wallet/negotiator.h" 

#include "utility/serialize.h"

namespace yas::detail
{
    template<std::size_t F>
    struct serializer<type_prop::not_a_fundamental
                    , ser_method::use_internal_serializer
                    , F
                    , beam::wallet::Negotiator>
    {
        template<typename Archive>
        static Archive& save(Archive& ar, const beam::wallet::Negotiator& sender)
        {
            const_cast<beam::wallet::Negotiator&>(sender).serialize(ar, 0);
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, beam::wallet::Negotiator& sender)
        {
            sender.serialize(ar, 0);
            return ar;
        }
    };

    template<std::size_t F, int NumberOfRegions>
    struct serializer<type_prop::not_a_fundamental
        , ser_method::use_internal_serializer
        , F
        , boost::msm::back::NoHistoryImpl<NumberOfRegions>>
    {
        using Type = boost::msm::back::NoHistoryImpl<NumberOfRegions>;
        template<typename Archive>
        static Archive& save(Archive& ar,const Type& d)
        {
            const_cast<Type&>(d).serialize(ar, 0);
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, Type& d)
        {
            d.serialize(ar, 0);
            return ar;
        }
    };

    template<std::size_t F>
    struct serializer<type_prop::not_a_fundamental
        , ser_method::use_internal_serializer
        , F
        , beam::wallet::Negotiator::FSMDefinition>
    {
        using Type = beam::wallet::Negotiator::FSMDefinition;
        template<typename Archive>
        static Archive& save(Archive& ar, const Type& d)
        {
            const_cast<Type&>(d).serialize(ar, 0);
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, Type& d)
        {
            d.serialize(ar, 0);
            return ar;
        }
    };

    template<std::size_t F>
    struct serializer<type_prop::not_a_fundamental
        , ser_method::use_internal_serializer
        , F
        , ECC::Scalar::Native>
    {
        using Type = ECC::Scalar::Native;
        template<typename Archive>
        static Archive& save(Archive& ar, const Type& v)
        {
            ECC::Scalar s;
            v.Export(s);
            ar & s;
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, Type& v)
        {
            ECC::Scalar s;
            ar & s;
            v.Import(s);
            return ar;
        }
    };

    template<std::size_t F>
    struct serializer<type_prop::not_a_fundamental
        , ser_method::use_internal_serializer
        , F
        , ECC::Point::Native>
    {
        using Type = ECC::Point::Native;
        template<typename Archive>
        static Archive& save(Archive& ar, const Type& v)
        {
            ECC::Point s;
            v.Export(s);
            ar & s;
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, Type& v)
        {
            ECC::Point s;
            ar & s;
            v.Import(s);
            return ar;
        }
    };
    template<std::size_t F>
    struct serializer<type_prop::not_a_fundamental
        , ser_method::use_internal_serializer
        , F
        , beam::Coin>
    {
        using Type = beam::Coin;
        template<typename Archive>
        static Archive& save(Archive& ar, const Type& v)
        {
            ar  & v.m_id
                & v.m_createHeight
                & v.m_maturity
                & v.m_key_type
                & v.m_amount
                & v.m_status;
            return ar;
        }

        template<typename Archive>
        static Archive& load(Archive& ar, Type& v)
        {
            // TODO: store id only
            ar & v.m_id
               & v.m_createHeight
               & v.m_maturity
               & v.m_key_type
               & v.m_amount
               & v.m_status;
            return ar;
        }
    };
}