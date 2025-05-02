#include "../common.h"
#include "../app_common_impl.h"
#include "../upgradable3/app_common_impl.h"
#include "contract_l1.h"

#define L2Tst1_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define L2Tst1_replace_admin(macro) Upgradable3_replace_admin(macro)
#define L2Tst1_set_min_approvers(macro) Upgradable3_set_min_approvers(macro)
#define L2Tst1_explicit_upgrade(macro) macro(ContractID, cid)

#define L2Tst1_my_admin_key(macro)

#define L2Tst1_deploy(macro) \
    Upgradable3_deploy(macro) \
    macro(Height, hPrePhaseEnd) \
    macro(AssetID, aidStaking) \
    macro(AssetID, aidLiquidity)

#define L2Tst1_view_deployed(macro)
#define L2Tst1_view_params(macro) macro(ContractID, cid)
#define L2Tst1_user_view(macro) macro(ContractID, cid)
#define L2Tst1_users_view_all(macro) macro(ContractID, cid)

#define L2Tst1_user_stake(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount)

#define L2Tst1_bridge_view_base(macro) \
    macro(Height, h0) \
    macro(uint32_t, owned_only)

#define L2Tst1_bridge_view(macro) \
    macro(ContractID, cid) \
    L2Tst1_bridge_view_base(macro)

#define L2Tst1_bridge_view_l2(macro) L2Tst1_bridge_view_base(macro)

#define L2Tst1_bridge_op_base(macro) \
    macro(Amount, amount) \
    macro(AssetID, aid) \
    macro(Cookie, cookie)

#define L2Tst1_bridge_op_l1(macro) \
    macro(ContractID, cid) \
    L2Tst1_bridge_op_base(macro)

#define L2Tst1_bridge_export(macro) L2Tst1_bridge_op_l1(macro)
#define L2Tst1_bridge_import(macro) L2Tst1_bridge_op_l1(macro)

#define L2Tst1_bridge_export_l2(macro) L2Tst1_bridge_op_base(macro)
#define L2Tst1_bridge_import_l2(macro) L2Tst1_bridge_op_base(macro)

#define L2Tst1Actions_All(macro) \
    macro(my_admin_key) \
    macro(view_deployed) \
    macro(view_params) \
    macro(deploy) \
	macro(schedule_upgrade) \
	macro(replace_admin) \
	macro(set_min_approvers) \
	macro(explicit_upgrade) \
	macro(user_view) \
	macro(users_view_all) \
	macro(user_stake) \
	macro(bridge_view) \
	macro(bridge_view_l2) \
	macro(bridge_export) \
	macro(bridge_import) \
	macro(bridge_export_l2) \
	macro(bridge_import_l2) \


namespace Env {
    void DocGet(const char* szID, L2Tst1_L1::Cookie& val)
    {
        auto ret = DocGetBlob(szID, val.m_p, sizeof(val.m_p));
        if (ret < sizeof(val.m_p))
            Env::Memset(val.m_p + ret, 0, sizeof(val.m_p) - ret);
    }
}

namespace L2Tst1_L1 {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("actions");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_ACTION(name) { Env::DocGroup gr(#name);  L2Tst1_##name(THE_FIELD) }
        
        L2Tst1Actions_All(THE_ACTION)
#undef THE_ACTION
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(name) void On_##name(L2Tst1_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr3-l2tst1";

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

const Upgradable3::Manager::VerInfo g_VerInfo = { s_pSID, _countof(s_pSID) };

ON_METHOD(my_admin_key)
{
    PubKey pk;
    AdminKeyID kid;
    kid.get_Pk(pk);
    Env::DocAddBlob_T("admin_key", pk);
}

ON_METHOD(view_deployed)
{
    AdminKeyID kid;
    g_VerInfo.DumpAll(&kid);
}

ON_METHOD(deploy)
{
    AdminKeyID kid;
    PubKey pk;
    kid.get_Pk(pk);

#pragma pack (push, 1)
    struct MyArg
        :public Method::Create
    {
        Validator m_pV[Validator::s_Max];
    } arg;
#pragma pack (pop)

    if (!g_VerInfo.FillDeployArgs(arg.m_Upgradable, &pk))
        return;

    if (hPrePhaseEnd <= Env::get_Height())
        return OnError("pre-phase too short");
    arg.m_Settings.m_hPreEnd = hPrePhaseEnd;

    if (!aidStaking || !aidLiquidity)
        return OnError("aids not provided");

    arg.m_Settings.m_aidStaking = aidStaking;
    arg.m_Settings.m_aidLiquidity = aidLiquidity;

    auto fValidator = Utils::MakeFieldIndex<Validator::s_Max>("validator_");
    for (arg.m_Validators = 0; arg.m_Validators < Validator::s_Max; arg.m_Validators++)
    {
        fValidator.Set(arg.m_Validators);
        if (!Env::DocGet(fValidator.m_sz, arg.m_pV[arg.m_Validators].m_pk))
            break;
    }

    if (!arg.m_Validators)
        return OnError("validators not specified");

    const uint32_t nCharge =
        Upgradable3::Manager::get_ChargeDeploy() +
        Env::Cost::SaveVar_For(sizeof(State)) +
        Env::Cost::SaveVar_For(sizeof(Validator) * arg.m_Validators) +
        Env::Cost::Cycle * 100;

    Env::GenerateKernel(nullptr, arg.s_iMethod, &arg, sizeof(Method::Create) + sizeof(Validator) * arg.m_Validators, nullptr, 0, nullptr, 0, "Deploy L2Tst1 contract", nCharge);
}

ON_METHOD(schedule_upgrade)
{
    AdminKeyID kid;
    g_VerInfo.ScheduleUpgrade(cid, kid, hTarget);
}

ON_METHOD(explicit_upgrade)
{
    const uint32_t nCharge =
        Env::Cost::LoadVar_For(sizeof(State)) +
        Env::Cost::SaveVar_For(sizeof(State)) +
        Env::Cost::Cycle * 300;

    Upgradable3::Manager::MultiSigRitual::Perform_ExplicitUpgrade(cid, nCharge);
}

ON_METHOD(replace_admin)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_ReplaceAdmin(cid, kid, iAdmin, pk);
}

ON_METHOD(set_min_approvers)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_SetApprovers(cid, kid, newVal);
}

struct MyState
    :public State
{
    bool Read(const ContractID& cid)
    {
        Env::Key_T<uint8_t> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        key.m_KeyInContract = Tags::s_State;

        if (Env::VarReader::Read_T(key, *this))
            return true;

        OnError("State not found");
        return false;
    }
};

struct Validators
{
    Validator m_pV[Validator::s_Max];
    uint32_t m_Count = 0;

    bool Read(const ContractID& cid)
    {
        Env::Key_T<uint8_t> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        key.m_KeyInContract = Tags::s_Validators;

        Env::VarReader r(key, key);

        uint32_t nKey = 0, nVal = sizeof(m_pV);
        if (!r.MoveNext(nullptr, nKey, m_pV, nVal, 0))
        {
            OnError("validators not found");
            return false;
        }

        if (!nVal || (nVal > sizeof(m_pV)) || (nVal % sizeof(Validator)))
        {
            OnError("validators not sane");
        }

        m_Count = nVal / sizeof(Validator);
        assert(m_Count && (m_Count <= Validator::s_Max));

        return true;
    }

    bool IsQuorumReached(uint32_t n)
    {
        return Validator::IsQuorumReached(n, m_Count);
    }
};

ON_METHOD(view_params)
{
    MyState s;
    if (!s.Read(cid))
        return;

    Env::DocGroup gr("res");
    Env::DocAddNum("aid-staking", s.m_Settings.m_aidStaking);
    Env::DocAddNum("aid-liquidity", s.m_Settings.m_aidLiquidity);
    Env::DocAddNum("hPreEnd", s.m_Settings.m_hPreEnd);

    Amount totalStake = WalkerFunds::FromContract_Lo(cid, s.m_Settings.m_aidStaking);
    Env::DocAddNum("total_stake", totalStake);

    Validators vals;
    if (vals.Read(cid))
    {
        Env::DocArray gr1("validators");

        for (uint32_t i = 0; i < vals.m_Count; i++)
            Env::DocAddBlob_T("", vals.m_pV[i].m_pk);
    }
}


struct MyUser
    :public User
{
    void Print(const Key& uk)
    {
        Env::DocAddBlob_T("key", uk.m_pk);
        Env::DocAddNum("stake", m_Stake);
    }

    struct KeyID :public Env::KeyID {
        KeyID(const ContractID& cid) :Env::KeyID(cid) {}
    };
};

ON_METHOD(user_view)
{
    Env::Key_T<User::Key> k;
    _POD_(k.m_Prefix.m_Cid) = cid;
    MyUser::KeyID(cid).get_Pk(k.m_KeyInContract.m_pk);

    MyUser u;
    if (Env::VarReader::Read_T(k, u))
    {
        Env::DocGroup gr("res");
        u.Print(k.m_KeyInContract);
    }
}

ON_METHOD(users_view_all)
{
    Env::DocArray gr0("res");

    Env::Key_T<User::Key> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k1.m_Prefix.m_Cid) = cid;

    _POD_(k0.m_KeyInContract.m_pk).SetZero();
    _POD_(k1.m_KeyInContract.m_pk).SetObject(0xff);

    for (Env::VarReader r(k0, k1); ; )
    {
        MyUser u;
        if (!r.MoveNext_T(k0, u))
            break;

        Env::DocGroup gr1("");
        u.Print(k0.m_KeyInContract);
    }
}

ON_METHOD(user_stake)
{
    if (!amount)
        return OnError("amount not specified");

    MyState s;
    if (!s.Read(cid))
        return;

    if (Env::get_Height() >= s.m_Settings.m_hPreEnd)
        return OnError("pre-phase is over");

    FundsChange fc;
    fc.m_Amount = amount;
    fc.m_Aid = s.m_Settings.m_aidStaking;
    fc.m_Consume = 1;

    uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::LoadVar_For(sizeof(State)) +
        Env::Cost::LoadVar_For(sizeof(User)) +
        Env::Cost::SaveVar_For(sizeof(User)) +
        Env::Cost::FundsLock +
        Env::Cost::Cycle * 200;


    Method::UserStake arg;
    arg.m_Amount = amount;
    MyUser::KeyID(cid).get_Pk(arg.m_pkUser);

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "L2Tst1 user stake", nCharge);
}

typedef L2Tst1_L2::Method::BridgeOpBase BridgeOpBase;

void ScanBridge(Env::Key_T<uint8_t>& key, Height h0, uint32_t owned_only)
{
    HeightPos pos;
    pos.m_Height = h0;
    pos.m_Pos = 0;

    Method::BridgeOp op;
    for (Env::LogReader r(key, key, &pos); r.MoveNext_T(key, op); )
    {
        PubKey pkMy;
        Env::KeyID(Cast::Down<BridgeOpBase>(op)).get_Pk(pkMy);

        bool bOwned = _POD_(pkMy) == op.m_pk;
        if (owned_only && !bOwned)
            continue;

        Env::DocGroup gr("");

        Env::DocAddNum("h", r.m_Pos.m_Height);
        Env::DocAddNum("aid", op.m_Aid);
        Env::DocAddNum("amount", op.m_Amount);
        Env::DocAddBlob_T("cookie", op.m_Cookie.m_p);
        Env::DocAddNum32("owned", !!bOwned);
    }

}

void ScanBridge(const ContractID& cid, Height h0, uint32_t owned_only, uint8_t tagExp, uint8_t tagImp)
{
    Env::Key_T<uint8_t> key;
    _POD_(key.m_Prefix.m_Cid) = cid;

    Env::DocGroup gr0("res");

    {
        Env::DocArray gr1("export");
        key.m_KeyInContract = tagExp;
        ScanBridge(key, h0, owned_only);
    }

    {
        Env::DocArray gr1("import");
        key.m_KeyInContract = tagImp;
        ScanBridge(key, h0, owned_only);
    }
}

ON_METHOD(bridge_view)
{
    ScanBridge(cid, h0, owned_only, Tags::s_BridgeExp, Tags::s_BridgeImp);
}

ON_METHOD(bridge_view_l2)
{
    ScanBridge(L2Tst1_L2::s_CID, h0, owned_only, 0, 1);
}

struct BridgeOpL1Context
{
    Env::KeyID m_kid;
    FundsChange m_fc;

    bool Init(L2Tst1_bridge_op_l1(THE_FIELD) Method::BridgeOp& arg, uint8_t tag)
    {
        if (!amount)
        {
            OnError("amount not specified");
            return false;
        }

        arg.m_Cookie = cookie;
        arg.m_Aid = aid;
        arg.m_Amount = amount;

        m_kid = Env::KeyID(Cast::Down<BridgeOpBase>(arg));
        m_kid.get_Pk(arg.m_pk);

        Env::Key_T<BridgeOpSave::Key> k0;
        _POD_(k0.m_Prefix.m_Cid) = cid;
        _POD_(k0.m_KeyInContract.m_pk) = arg.m_pk;
        k0.m_KeyInContract.m_Tag = tag;

        BridgeOpSave val;
        if (Env::VarReader::Read_T(k0, val))
        {
            OnError("op duplicated");
            Env::DocAddNum("hPrev", val.m_Height);
            return false;
        }

        m_fc.m_Aid = aid;
        m_fc.m_Amount = amount;

        return true;
    }

};


ON_METHOD(bridge_export)
{
    BridgeOpL1Context ctx;
    Method::BridgeExport arg;
    if (!ctx.Init(cid, amount, aid, cookie, arg, Tags::s_BridgeExp))
        return;
    ctx.m_fc.m_Consume = 1;

    const uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::SaveVar_For(sizeof(BridgeOpSave)) +
        Env::Cost::Log_For(sizeof(Method::BridgeOp)) +
        Env::Cost::FundsLock +
        Env::Cost::Cycle * 100;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &ctx.m_fc, 1, nullptr, 0, "bridge export", nCharge);
}

ON_METHOD(bridge_import)
{
    BridgeOpL1Context ctx;
    Method::BridgeImport arg;
    if (!ctx.Init(cid, amount, aid, cookie, arg, Tags::s_BridgeExp))
        return;
    ctx.m_fc.m_Consume = 0;

    Validators vals;
    if (!vals.Read(cid))
        return;

    // create random address for comm
    HashValue hvBbs;
    Env::GenerateRandom(hvBbs.m_p, sizeof(hvBbs.m_p));
    Env::Comm_Listen(hvBbs.m_p, sizeof(hvBbs.m_p), 0);

    Msg::GetNonce msg1;
    msg1.m_ProtoVer = Msg::s_ProtoVer;
    _POD_(msg1.m_pkOwner) = arg.m_pk;
    Env::DerivePk(msg1.m_pkBbs, hvBbs.m_p, sizeof(hvBbs.m_p));

    for (uint32_t i = 0; i < vals.m_Count; i++)
        Env::Comm_Send(vals.m_pV[i].m_pk, &msg1, sizeof(msg1));

    static const uint32_t iSlotKrnBlind = 0;
    static const uint32_t iSlotKrnNonce = 1;
    static const uint32_t iSlotKeyNonce = 2;

    Secp::Point pt0, pt1;
    pt0.FromSlot(iSlotKrnNonce);

    uint32_t nCountApproved = 0;
    arg.m_ApproveMask = 0;

    Env::WriteStr("waiting for validator nonces...", Stream::Out);

    while (true)
    {
        Env::Comm_WaitMsg(5000);

        Msg::Nonce msgIn;
        uint32_t nRet = Env::Comm_Read(&msgIn, sizeof(msgIn), nullptr, 0);
        if (nRet < sizeof(msgIn))
        {
            if (!nRet && vals.IsQuorumReached(nCountApproved))
                break; // enough

            continue;
        }

        if (msgIn.s_OpCode != msgIn.m_OpCode)
            continue;

        if (msgIn.m_iValidator >= vals.m_Count)
            continue;

        uint32_t msk = 1u << msgIn.m_iValidator;
        if (arg.m_ApproveMask & msk)
            continue;

        if (!pt1.Import(msgIn.m_m_Nonce))
            continue;

        if (pt1.IsZero())
            continue;

        pt0 += pt1;
        arg.m_ApproveMask |= msk;
        nCountApproved++;

        if (nCountApproved == vals.m_Count)
            break; // max validators responded
    }

    pt1.FromSlot(iSlotKeyNonce);
    pt0 += pt1;

    uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::SaveVar_For(sizeof(BridgeOpSave)) +
        Env::Cost::Log_For(sizeof(Method::BridgeOp)) +
        Env::Cost::FundsLock +
        Env::Cost::AddSig * (1 + nCountApproved) +
        Env::Cost::Cycle * 100;

    Msg::GetSignature msg2;
    _POD_(msg2.m_pkOwner) = msg1.m_pkOwner;
    _POD_(msg2.m_Cookie) = arg.m_Cookie;
    msg2.m_nApproveMask = arg.m_ApproveMask;
    pt0.Export(msg2.m_TotalNonce);
    msg2.m_hMin = Env::get_Height();
    msg2.m_nCharge = nCharge;

    pt0.FromSlot(iSlotKrnBlind);
    PubKey pkBlind;
    pt0.Export(pkBlind);

    Secp::Scalar s0, s1;
    Env::Secp_Scalar_set(s1, amount);
    Env::Secp_Point_mul_H(pt1, s1, aid);
    Env::Secp_Point_neg(pt1, pt1);

    pt1 += pt0;
    pt1.Export(msg2.m_Commitment);

    PubKey pSigs[Validator::s_Max + 1];
    _POD_(pSigs[0]) = arg.m_pk;
    uint32_t nSigs = 1;

    for (uint32_t i = 0; i < vals.m_Count; i++)
    {
        if ((1u << i) & arg.m_ApproveMask)
        {
            auto& addr = vals.m_pV[i].m_pk;
            _POD_(pSigs[nSigs++]) = addr;

            Env::Comm_Send(addr, &msg2, sizeof(msg2));
        }
    }

    // get challenges
    Secp_scalar_data pE[Validator::s_Max + 1];
    Env::GenerateKernelAdvanced(&cid, arg.s_iMethod, &arg, sizeof(arg), &ctx.m_fc, 1, pSigs, nSigs, "bridge import", nCharge, msg2.m_hMin, msg2.m_hMin + Msg::s_dh, pkBlind, msg2.m_TotalNonce, pE[0], iSlotKrnBlind, iSlotKrnNonce, pE);

    s1.Import(pE[0]); // challenge for our key
    ctx.m_kid.get_Blind(s0, s1, iSlotKeyNonce);

    Env::WriteStr("waiting for validator signatures...", Stream::Out);

    for (uint32_t nMskRemaining = arg.m_ApproveMask; ; )
    {
        Env::Comm_WaitMsg(0);

        Msg::Signature msgIn;
        uint32_t nRet = Env::Comm_Read(&msgIn, sizeof(msgIn), nullptr, 0);
        if (nRet < sizeof(msgIn))
            continue;

        if (msgIn.s_OpCode != msgIn.m_OpCode)
            continue;

        if (msgIn.m_iValidator >= vals.m_Count)
            continue;

        uint32_t msk = 1u << msgIn.m_iValidator;
        if (!(nMskRemaining & msk))
            continue;

        if (!s1.Import(msgIn.m_k))
            continue;

        s0 += s1;

        nMskRemaining &= ~msk;
        if (!nMskRemaining)
            break;
    }

    // final call
    s0.Export(pE[0]);
    Env::GenerateKernelAdvanced(&cid, arg.s_iMethod, &arg, sizeof(arg), &ctx.m_fc, 1, pSigs, nSigs, "bridge import", nCharge, msg2.m_hMin, msg2.m_hMin + Msg::s_dh, pkBlind, msg2.m_TotalNonce, pE[0], iSlotKrnBlind, iSlotKrnNonce, nullptr);
}

void OnBridgeL2(L2Tst1_bridge_op_base(THE_FIELD) uint32_t iMethod, uint8_t consume, const char* szComment)
{
    if (!amount)
        return OnError("amount not specified");

    Method::BridgeOp arg;
    arg.m_Cookie = cookie;
    arg.m_Aid = aid;
    arg.m_Amount = amount;

    Env::KeyID kid(Cast::Down<BridgeOpBase>(arg));
    kid.get_Pk(arg.m_pk);

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Consume = consume;

    const uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::Log_For(sizeof(Method::BridgeOp)) +
        Env::Cost::FundsLock +
        Env::Cost::AssetEmit +
        Env::Cost::AddSig +
        Env::Cost::Cycle * 100;

    Env::GenerateKernel(&L2Tst1_L2::s_CID, iMethod, &arg, sizeof(arg), &fc, 1, &kid, 1, "bridge export", nCharge);
}

ON_METHOD(bridge_export_l2)
{
    OnBridgeL2(amount, aid, cookie, L2Tst1_L2::Method::BridgeBurn::s_iMethod, 1, "bridge L2 export");
}

ON_METHOD(bridge_import_l2)
{
    OnBridgeL2(amount, aid, cookie, L2Tst1_L2::Method::BridgeEmit::s_iMethod, 0, "bridge L2 import");
}

#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
{
    Env::DocGroup root("");

    char szAction[0x20];

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(name) \
        static_assert(sizeof(szAction) >= sizeof(#name)); \
        if (!Env::Strcmp(szAction, #name)) { \
            L2Tst1_##name(PAR_READ) \
            On_##name(L2Tst1_##name(PAR_PASS) 0); \
            return; \
        }

    L2Tst1Actions_All(THE_METHOD)

#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown action");
}

} // namespace L2Tst1
