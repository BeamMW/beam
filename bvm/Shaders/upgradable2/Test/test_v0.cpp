////////////////////////
#include "../../common.h"
#include "test.h"

struct StatePlus
    :public Upgradable2::Test::State
{
    bool Load()
    {
        return Env::LoadVar_T((uint8_t) s_Key, *this);
    }

    void Save() const
    {
        Env::SaveVar_T((uint8_t) s_Key, *this);
    }
};

BEAM_EXPORT void Ctor(void*)
{
    StatePlus s;
    s.m_iVer = 0;
    s.Save();
}

BEAM_EXPORT void Dtor(void* pArg)
{
    Env::DelVar_T((uint8_t) StatePlus::s_Key);
}

BEAM_EXPORT void Method_2(void*)
{
    Env::Halt(); // should not be called for the very first version
}

BEAM_EXPORT void Method_3(const Upgradable2::Test::SomeMethod& r)
{
    Env::Halt_if(r.m_ExpectedVer != 0);
}
