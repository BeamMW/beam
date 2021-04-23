#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Sidechain_client_create(macro) \
    macro(HashValue, rulesCfg) \
    macro(uint32_t, verifyPoW) \
    macro(Amount, comission) \
    macro(BlockHeader::Full, hdr0) \

#define Sidechain_client_view(macro)
#define Sidechain_client_view_params(macro) macro(ContractID, cid)

#define Sidechain_client_get_State(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, nHdrsCount)

#define Sidechain_client_send_new_hdrs(macro) \
    macro(ContractID, cid) \
    macro(BlockHeader::Prefix, prefix) \
    macro(uint32_t, nHdrsCount)

#define SidechainRole_client(macro) \
    macro(client, create) \
    macro(client, view) \
    macro(client, view_params) \
    macro(client, get_State) \
    macro(client, send_new_hdrs) \

#define Sidechain_server_get_init_params(macro) macro(Height, h0)
#define Sidechain_server_get_new_hdrs(macro) \
    macro(Height, hTop) \
    macro(uint32_t, nHdrsCount)

#define SidechainRole_server(macro) \
    macro(server, get_init_params) \
    macro(server, get_new_hdrs) \

#define SidechainRoles_All(macro) \
    macro(client) \
    macro(server) \

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Sidechain_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); SidechainRole_##name(THE_METHOD) }
        
        SidechainRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Sidechain_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}


ON_METHOD(client, view)
{
    EnumAndDumpContracts(Sidechain::s_SID);
}

ON_METHOD(client, create)
{
    Sidechain::Init pars;

    _POD_(pars.m_Rules) = rulesCfg;
    pars.m_VerifyPoW = !!verifyPoW;
    pars.m_ComissionForProof = comission;
    _POD_(pars.m_Hdr0) = hdr0;

    // Create kernel with all the required parameters
    // 
    Env::GenerateKernel(nullptr, pars.s_iMethod, &pars, sizeof(pars), nullptr, 0, nullptr, 0, "generate Sidechain contract", 0);
}

ON_METHOD(client, view_params)
{
    const Sidechain::Global* pG;
    if (!Env::VarRead_T((uint8_t) 0, pG))
    {
        OnError("no global data");
        return;
    }

    Env::DocGroup gr("params");

    Env::DocAddNum("Height", pG->m_Height);

    HashValue hv;
    pG->m_Chainwork.ToBE_T(hv);
    Env::DocAddBlob_T("Chainwork", hv);
}

ON_METHOD(server, get_init_params)
{
    BlockHeader::Full s;
    s.m_Height = h0;
    Env::get_HdrFull(s);

    Env::DocAddBlob_T("hdr0", s);

    HashValue hv;
    Env::get_RulesCfg(h0, hv);
    Env::DocAddBlob_T("cfg", hv);
}

ON_METHOD(client, get_State)
{
    const Sidechain::Global* pG;
    if (!Env::VarRead_T((uint8_t)0, pG))
    {
        OnError("no global data");
        return;
    }
    Height h = pG->m_Height;
    Env::DocAddNum("height", h);

    uint32_t nSizeHashes = sizeof(HashValue) * nHdrsCount;
    auto* pHashes = (HashValue*) Env::StackAlloc(nSizeHashes);

    for (uint32_t i = 0; i < nHdrsCount; i++, h--)
    {
        const Sidechain::PerHeight* pV;
        if (!Env::VarRead_T(h, pV))
        {
            OnError("no height data");
            return;
        }

        _POD_(pHashes[i]) = pV->m_Hash;
    }

    Env::DocAddBlob("hashes", pHashes, nSizeHashes);
}

void DeriveMyPk(PubKey& pubKey, const ContractID& cid)
{
    Env::DerivePk(pubKey, &cid, sizeof(cid));
}

ON_METHOD(client, send_new_hdrs)
{
    uint32_t nSizeElems = sizeof(BlockHeader::Element) * nHdrsCount;

    typedef Sidechain::Grow<0> Request;
    uint32_t nArgSize = sizeof(Request) + nSizeElems;
    Request* pRequest = (Request*) Env::StackAlloc(nArgSize);

    pRequest->m_nSequence = nHdrsCount;
    _POD_(pRequest->m_Prefix) = prefix;

    if (!Env::DocGetBlobEx("hdrs", pRequest->m_pSequence, nSizeElems))
    {
        OnError("couldn't read hdrs");
        return;
    }

    DeriveMyPk(pRequest->m_Contributor, cid);

    Env::GenerateKernel(&cid, Request::s_iMethod, pRequest, nArgSize, nullptr, 0, nullptr, 0, "Sidechain new hdrs", 0);
}

ON_METHOD(server, get_new_hdrs)
{
    uint32_t nSizeHashes = sizeof(HashValue) * nHdrsCount;
    auto* pHashes = (HashValue*) Env::StackAlloc(nSizeHashes);

    if (!Env::DocGetBlobEx("hashes", pHashes, nSizeHashes))
    {
        OnError("couldn't read hashes");
        return;
    }

    Height hLocal = Env::get_Height();
    uint32_t nReorg = 0;
    for (; ; nReorg++)
    {
        if (nReorg == nHdrsCount)
        {
            OnError("can't find branch point. Retry with longer backlog");
            return;
        }

        BlockHeader::Info hdr;
        hdr.m_Height = hTop - nReorg;
        if (hdr.m_Height <= hLocal)
        {
            Env::get_HdrInfo(hdr);

            if (_POD_(hdr.m_Hash) == pHashes[nReorg])
                break;
        }
    }

    Env::StackFree(nSizeHashes);

    BlockHeader::Full s;
    s.m_Height = hTop - nReorg;
    if (s.m_Height == hLocal)
    {
        OnError("no new headers");
        return;
    }

    uint32_t nNewHdrs = static_cast<uint32_t>(hLocal - s.m_Height);
    uint32_t nSizeNewHdrs = sizeof(BlockHeader::Element) * nNewHdrs;
    auto* pElem = (BlockHeader::Element*) Env::StackAlloc(nSizeNewHdrs);

    Env::DocAddBlob_T("nHdrsCount", nNewHdrs);

    for (uint32_t i = 0; i < nNewHdrs; i++)
    {
        s.m_Height++;
        Env::get_HdrFull(s);

        if (!i)
            Env::DocAddBlob_T("prefix", Cast::Down<BlockHeader::Prefix>(s));

        _POD_(pElem[i]) = Cast::Down<BlockHeader::Element>(s);
    }

    Env::DocAddBlob("hdrs", pElem, nSizeNewHdrs);
}

#undef ON_METHOD
#undef THE_FIELD

namespace Env {

    inline bool DocGet(const char* szID, BlockHeader::Full& val) {
        return DocGetBlobEx(szID, &val, sizeof(val));
    }

    inline bool DocGet(const char* szID, BlockHeader::Prefix& val) {
        return DocGetBlobEx(szID, &val, sizeof(val));
    }

} // namespace Env

export void Method_1() 
{
    Env::DocGroup root("");

    char szRole[0x10], szAction[0x10];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

    const char* szErr = nullptr;

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Sidechain_##role##_##name(PAR_READ) \
            On_##role##_##name(Sidechain_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        SidechainRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    SidechainRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

