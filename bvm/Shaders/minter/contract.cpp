////////////////////////
#include "../common.h"
#include "contract.h"
#include "../dao-vault/contract.h"

namespace Minter {


BEAM_EXPORT void Ctor(const Method::Init& r)
{
    Env::Halt_if(!Env::RefAdd(r.m_Settings.m_cidDaoVault));
    Env::SaveVar_T(Utils::toValue(Tags::s_Settings), r.m_Settings);
}

BEAM_EXPORT void Dtor(void*)
{
    Settings s;
    Env::LoadVar_T(Utils::toValue(Tags::s_Settings), s);
    Env::RefRelease(s.m_cidDaoVault);
}

BEAM_EXPORT void Method_2(Method::View& r)
{
    Token::Key tk;
    tk.m_Aid = r.m_Aid;

    if (!Env::LoadVar_T(tk, r.m_Result))
        _POD_(r.m_Result).SetZero();
}

void AddSigPlus(const PubKey& pk)
{
    switch (pk.m_Y)
    {
    case 0:
    case 1:
        Env::AddSig(pk);
        break;

    case PubKeyFlag::s_Cid:
    {
        // contract
        ContractID cid;
        Env::get_CallerCid(1, cid);
        Env::Halt_if(_POD_(cid) != pk.m_X);
    }
    break;

    default:
        Env::Halt(); // invalid
    }
}

BEAM_EXPORT void Method_3(Method::CreateToken& r)
{
    Token::Key tk;
    tk.m_Aid = Env::AssetCreate(&r + 1, r.m_MetadataSize);
    Env::Halt_if(!tk.m_Aid);

    Token t;
    _POD_(t).SetZero();
    t.m_Limit = r.m_Limit;
    _POD_(t.m_pkOwner) = r.m_pkOwner;

    Env::SaveVar_T(tk, t);

    r.m_Aid = tk.m_Aid; // retval

    Settings s;
    Env::LoadVar_T(Utils::toValue(Tags::s_Settings), s);

    DaoVault::Method::Deposit arg;
    arg.m_Aid = 0;
    arg.m_Amount = s.m_IssueFee;
    Env::CallFar_T(s.m_cidDaoVault, arg/*, CallFarFlags::SelfLockRO*/);

}

BEAM_EXPORT void Method_4(Method::Withdraw& r)
{
    Token::Key tk;
    tk.m_Aid = r.m_Aid;

    Token t;
    Env::Halt_if(!Env::LoadVar_T(tk, t));

    t.m_Minted += r.m_Value; // woulf fail on overflow

    // check limits
    bool bOverflow =
        (t.m_Minted.m_Hi > t.m_Limit.m_Hi) ||
        ((t.m_Minted.m_Hi == t.m_Limit.m_Hi) && (t.m_Minted.m_Lo > t.m_Limit.m_Lo));
    Env::Halt_if(bOverflow);

    Env::SaveVar_T(tk, t);
    AddSigPlus(t.m_pkOwner);

    Env::Halt_if(!Env::AssetEmit(tk.m_Aid, r.m_Value, 1));
    Env::FundsUnlock(tk.m_Aid, r.m_Value);
}

} // namespace Minter
