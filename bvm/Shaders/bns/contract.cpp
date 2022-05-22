////////////////////////
#include "../common.h"
#include "contract.h"

namespace NameService {

BEAM_EXPORT void Ctor(void*)
{
}

BEAM_EXPORT void Dtor(void*)
{
}

struct MyDomain
    :public Domain
{
    Domain::KeyMax m_Key;
    uint32_t m_KeyLen;

    MyDomain(const uint8_t& nNameLen)
    {
        uint32_t delta = nNameLen - s_MinLen;
        Env::Halt_if(delta > s_MaxLen - s_MinLen);

        Env::Memcpy(m_Key.m_sz, &nNameLen + 1, nNameLen);
        m_KeyLen = nNameLen + 1;
    }

    bool Load()
    {
        return Env::LoadVar(&m_Key, m_KeyLen, &Cast::Down<Domain>(*this), sizeof(Domain), KeyTag::Internal) == sizeof(Domain);
    }

    bool Save() const
    {
        return Env::SaveVar(&m_Key, m_KeyLen, &Cast::Down<Domain>(*this), sizeof(Domain), KeyTag::Internal) == sizeof(Domain);
    }
};

void ChargePrice(uint8_t nNameLen)
{
    Amount amount =
        (nNameLen <= 3) ? Domain::s_Price3 :
        (nNameLen <= 4) ? Domain::s_Price4 :
        Domain::s_Price5;

    Env::FundsLock(0, amount);
}

BEAM_EXPORT void Method_2(const Method::Register& r)
{
    MyDomain d(r.m_NameLen);
    if (d.Load())
        // make sure it's expired
        Env::Halt_if(Env::get_Height() < d.m_hExpire + Domain::s_PeriodHold);
    else
    {
        // check name
        for (uint32_t i = 0; i < r.m_NameLen; i++)
            Env::Halt_if(!d.IsValidChar(d.m_Key.m_sz[i]));
    }

    ChargePrice(r.m_NameLen);
    d.m_hExpire = Env::get_Height() + Domain::s_PeriodValidity;
    _POD_(d.m_pkOwner) = r.m_pkOwner;

    d.Save();
}

BEAM_EXPORT void Method_3(const Method::SetOwner& r)
{
    MyDomain d(r.m_NameLen);
    Env::Halt_if(!d.Load());

    Env::AddSig(d.m_pkOwner);
    _POD_(d.m_pkOwner) = r.m_pkNewOwner;


    d.Save();
}

BEAM_EXPORT void Method_4(const Method::Extend& r)
{
    MyDomain d(r.m_NameLen);
    Env::Halt_if(!d.Load());

    Height h = Env::get_Height();
    if (d.m_hExpire < h)
        d.m_hExpire = h;

    d.m_hExpire += Domain::s_PeriodValidity;
    ChargePrice(r.m_NameLen);

    d.Save();
}


} // namespace NameService
