#include "../common.h"
#include "contract.h"

namespace AssetMan {

BEAM_EXPORT void Ctor(const void*)
{
}

BEAM_EXPORT void Dtor(const void*)
{
}

BEAM_EXPORT void Method_2(const void*)
{
    static const char szMeta[] = "STD:SCH_VER=1;N=AssetMan contract token;SN=Amt;UN=AMT;NTHUN=GROTH";
    auto aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
    Env::Halt_if(!aid);
}

BEAM_EXPORT void Method_3(const Method::AssetUnreg& r)
{
    Env::Halt_if(!Env::AssetDestroy(r.m_Aid));
}

} // namespace AssetMan
