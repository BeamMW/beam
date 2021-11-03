#include "../common.h"
#include "../Math.h"
#include "contract.h"

struct MyState
    :public Gallery::State
{
    MyState() {
        Env::LoadVar_T((uint8_t) s_Key, *this);
    }

    MyState(bool) {
        // no auto-load
    }

    void Save() {
        Env::SaveVar_T((uint8_t) s_Key, *this);
    }

    void AddSigAdmin() {
        Env::AddSig(m_Config.m_pkAdmin);
    }
};


BEAM_EXPORT void Ctor(const Gallery::Method::Init& r)
{
    if (Env::get_CallDepth() > 1)
    {
        MyState s(false);
        s.m_Exhibits = 0;
        s.m_VoteBalance = 0;
        _POD_(s.m_Config) = r.m_Config;

        s.Save();
    }
}

BEAM_EXPORT void Dtor(void*)
{
    // ignore
}

void PayoutMove(const Gallery::Payout::Key& key, Amount val, bool bAdd)
{
    if (!val)
        return;

    Gallery::Payout po;
    if (Env::LoadVar_T(key, po))
    {
        if (bAdd)
            Strict::Add(po.m_Amount, val);
        else
        {
            Strict::Sub(po.m_Amount, val);

            if (!po.m_Amount)
            {
                Env::DelVar_T(key);
                return;
            }
        }
    }
    else
    {
        Env::Halt_if(!bAdd);
        po.m_Amount = val;
    }

    Env::SaveVar_T(key, po);
}

BEAM_EXPORT void Method_2(void*)
{
    // called on upgrade
}

BEAM_EXPORT void Method_10(const Gallery::Method::ManageArtist& r)
{
    Gallery::Artist::Key ak;
    _POD_(ak.m_pkUser) = r.m_pkArtist;

    if (r.m_LabelLen <= Gallery::Artist::s_LabelMaxLen)
    {
        struct ArtistPlus : public Gallery::Artist {
            char m_szLabel[s_LabelMaxLen];
        } a;

        a.m_hRegistered = Env::get_Height();
        Env::Memcpy(a.m_szLabel, &r + 1, r.m_LabelLen);

        Env::Halt_if(Env::SaveVar(&ak, sizeof(ak), &a, sizeof(Gallery::Artist) + r.m_LabelLen, KeyTag::Internal)); // will fail if already exists
    } else
        Env::Halt_if(!Env::DelVar_T(ak)); // will fail if doesn't exist

    MyState s;
    s.AddSigAdmin();
}

BEAM_EXPORT void Method_3(const Gallery::Method::AddExhibit& r)
{
    MyState s;

    Gallery::Masterpiece::Key mk;
    mk.m_ID = Utils::FromBE(++s.m_Exhibits);
    s.Save();

    Gallery::Masterpiece m;
    _POD_(m).SetZero();
    _POD_(m.m_pkOwner) = r.m_pkArtist;

    auto pData = reinterpret_cast<const uint8_t*>(&r + 1);
    uint32_t nData = r.m_Size;

    {
        // verify artist
        Gallery::Artist::Key ak;
        _POD_(ak.m_pkUser) = r.m_pkArtist;

        Gallery::Artist a;
        Env::Halt_if(!Env::LoadVar(&ak, sizeof(ak), &a, sizeof(a), KeyTag::Internal));
    }

    Env::SaveVar_T(mk, m);

    s.AddSigAdmin();

    Gallery::Events::Add::Key eak;
    eak.m_ID = mk.m_ID;
    _POD_(eak.m_pkArtist) = m.m_pkOwner;

    uint32_t nMaxEventSize = 0x2000; // TODO: max event size is increased to 1MB from HF4

    while (true)
    {
        Env::EmitLog(&eak, sizeof(eak), pData, std::min(nData, nMaxEventSize), KeyTag::Internal);
        if (nData <= nMaxEventSize)
            break;

        nData -= nMaxEventSize;
        pData += nMaxEventSize;
    }
    
}

BEAM_EXPORT void Method_4(const Gallery::Method::SetPrice& r)
{
    Gallery::Masterpiece::Key mk;
    mk.m_ID = r.m_ID;
    Gallery::Masterpiece m;
    Env::Halt_if(!Env::LoadVar_T(mk, m));

    _POD_(m.m_Price) = r.m_Price;
    Env::SaveVar_T(mk, m);

    Env::AddSig(m.m_pkOwner); // would fail if no current owner (i.e. checked out)
}

BEAM_EXPORT void Method_5(const Gallery::Method::Buy& r)
{
    Gallery::Masterpiece::Key mk;
    mk.m_ID = r.m_ID;
    Gallery::Masterpiece m;
    Env::Halt_if(!Env::LoadVar_T(mk, m));

    Env::Halt_if(
        !m.m_Price.m_Amount || // not for sale!
        (r.m_PayMax < m.m_Price.m_Amount) || // too expensive
        (r.m_HasAid != (!!m.m_Aid))
    );

    Env::FundsLock(m.m_Price.m_Aid, r.m_PayMax);

    Gallery::Events::Sell::Key esk;
    esk.m_ID = r.m_ID;
    Gallery::Events::Sell es;
    _POD_(es.m_Price) = m.m_Price;
    es.m_HasAid = r.m_HasAid;
    Env::EmitLog_T(esk, es);

    Gallery::Payout::Key pok;
    pok.m_ID = r.m_ID;
    pok.m_Aid = m.m_Price.m_Aid;

    _POD_(pok.m_pkUser) = m.m_pkOwner;
    PayoutMove(pok, m.m_Price.m_Amount, true);

    _POD_(pok.m_pkUser) = r.m_pkUser;
    PayoutMove(pok, r.m_PayMax - m.m_Price.m_Amount, true);

    _POD_(m.m_pkOwner) = r.m_pkUser;
    _POD_(m.m_Price).SetZero(); // not for sale until new owner sets the price
    Env::SaveVar_T(mk, m);

    //Env::AddSig(r.m_pkUser);
}

BEAM_EXPORT void Method_6(const Gallery::Method::Withdraw& r)
{
    PayoutMove(r.m_Key, r.m_Value, false);
    Env::FundsUnlock(r.m_Key.m_Aid, r.m_Value);
    Env::AddSig(r.m_Key.m_pkUser);
}

BEAM_EXPORT void Method_7(const Gallery::Method::CheckPrepare& r)
{
    Gallery::Masterpiece::Key mk;
    mk.m_ID = r.m_ID;
    Gallery::Masterpiece m;
    Env::Halt_if(!Env::LoadVar_T(mk, m));
    Env::AddSig(m.m_pkOwner);

    if (m.m_Aid)
    {
        // destroy it
        Env::Halt_if(!Env::AssetDestroy(m.m_Aid));
        m.m_Aid = 0;
    }
    else
    {
        // 1st call. Don't checkout, only prepare
        static const char szMeta[] = "STD:SCH_VER=1;N=Gallery Masterpiece;SN=Gall;UN=GALL;NTHUN=unique";
        m.m_Aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
    }

    Env::SaveVar_T(mk, m);
}

BEAM_EXPORT void Method_8(const Gallery::Method::CheckOut& r)
{
    Gallery::Masterpiece::Key mk;
    mk.m_ID = r.m_ID;
    Gallery::Masterpiece m;
    Env::Halt_if(!Env::LoadVar_T(mk, m) || !m.m_Aid);
    Env::AddSig(m.m_pkOwner);

    Env::Halt_if(!Env::AssetEmit(m.m_Aid, 1, 1));
    Env::FundsUnlock(m.m_Aid, 1);

    _POD_(m.m_pkOwner).SetZero();
    _POD_(m.m_Price).SetZero();

    Env::SaveVar_T(mk, m);
}

BEAM_EXPORT void Method_9(const Gallery::Method::CheckIn& r)
{
    Gallery::Masterpiece::Key mk;
    mk.m_ID = r.m_ID;
    Gallery::Masterpiece m;
    Env::Halt_if(!Env::LoadVar_T(mk, m) || !_POD_(m.m_pkOwner).IsZero());

    Env::FundsLock(m.m_Aid, 1);
    Env::Halt_if(!Env::AssetEmit(m.m_Aid, 1, 0));

    _POD_(m.m_pkOwner) = r.m_pkUser;
    Env::SaveVar_T(mk, m);

    //Env::AddSig(r.m_pkUser);
}

BEAM_EXPORT void Method_11(const Gallery::Method::Vote& r)
{
    Gallery::Impression::Key impk;
    _POD_(impk.m_ID) = r.m_ID;

    Gallery::Impression imp;
    if (!Env::LoadVar_T(impk, imp))
    {
        imp.m_Value = 0;

        MyState s;
        Strict::Sub(s.m_VoteBalance, s.m_Config.m_VoteReward.m_Amount);
        s.Save();

        Env::Halt_if(Utils::FromBE(impk.m_ID.m_MasterpieceID) > s.m_Exhibits);

        Env::FundsUnlock(s.m_Config.m_VoteReward.m_Aid, s.m_Config.m_VoteReward.m_Amount);
    }

    Env::Halt_if(imp.m_Value == r.m_Impression.m_Value); // not changed

    imp.m_Value = r.m_Impression.m_Value;
    Env::SaveVar_T(impk, imp);

    Env::AddSig(impk.m_ID.m_pkUser);
}

BEAM_EXPORT void Method_12(const Gallery::Method::AddVoteRewards& r)
{
    MyState s;
    Strict::Add(s.m_VoteBalance, r.m_Amount);
    s.Save();

    Env::FundsLock(s.m_Config.m_VoteReward.m_Aid, r.m_Amount);
}
