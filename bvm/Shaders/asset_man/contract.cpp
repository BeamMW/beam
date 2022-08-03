#include "../common.h"
#include "contract.h"

namespace AssetMan {

BEAM_EXPORT void Ctor(const void*)
{
}

BEAM_EXPORT void Dtor(const void*)
{
}

BEAM_EXPORT void Method_2(const Method::AssetReg& r)
{
    auto aid = Env::AssetCreate(&r + 1, r.m_SizeMetadata);
    Env::Halt_if(!aid);
}

BEAM_EXPORT void Method_3(const Method::AssetUnreg& r)
{
    Env::Halt_if(!Env::AssetDestroy(r.m_Aid));
}

} // namespace AssetMan
