////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"

BEAM_EXPORT void Ctor(void*)
{
    Liquity::Global s;
    _POD_(s).SetZero();

    s.m_StabPool.Init();
    s.m_Troves.m_Liquidated.Init();

    static const char szMeta[] = "STD:SCH_VER=1;N=Liquity Token;SN=Liqt;UN=LIQT;NTHUN=GROTHL";
    s.m_Aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
    Env::Halt_if(!s.m_Aid);

    auto key = Liquity::Tags::s_State;
    Env::SaveVar_T(key, s);
}

BEAM_EXPORT void Dtor(void*)
{
}
