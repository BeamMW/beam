#include "../common.h"
#include "../pipe/contract.h"
#include "contract.h"

export void Ctor(const MirrorCoin::Create0& r)
{
    MirrorCoin::Global g;
    Utils::Copy(g.m_PipeID, r.m_PipeID);
    Utils::ZeroObject(g.m_Remote);

    Env::Halt_if(!Env::RefAdd(r.m_PipeID));

    g.m_IsMirror = !!r.m_MetadataSize;
    if (g.m_IsMirror)
    {
        Env::Halt_if(r.m_Aid);
        g.m_Aid = Env::AssetCreate(&r + 1, r.m_MetadataSize);
        Env::Halt_if(!g.m_Aid);
    }
    else
        g.m_Aid = r.m_Aid;

    uint8_t gk = 0;
    Env::SaveVar_T(gk, g);
}

export void Dtor(void*)
{
    MirrorCoin::Global g;
    uint8_t gk = 0;
    Env::LoadVar_T(gk, g);

    Env::Halt_if(!Env::RefRelease(g.m_PipeID));

    if (g.m_IsMirror)
        Env::Halt_if(!Env::AssetDestroy(g.m_Aid));

    Env::DelVar_T(gk);
}

export void Method_2(const MirrorCoin::SetRemote& r)
{
    MirrorCoin::Global g;
    uint8_t gk = 0;
    Env::LoadVar_T(gk, g);

    Env::Halt_if(!Utils::IsZero(g.m_Remote) || Utils::IsZero(r.m_Cid));
    Utils::Copy(g.m_Remote, r.m_Cid);

    Env::SaveVar_T(gk, g);
}

export void Method_3(const MirrorCoin::Send& r)
{
    MirrorCoin::Global g;
    uint8_t gk = 0;
    Env::LoadVar_T(gk, g);

    Env::Halt_if(Utils::IsZero(g.m_Remote));

#pragma pack (push, 1)
    struct Arg :public Pipe::PushLocal0
    {
        MirrorCoin::Message m_Msg;
    };
#pragma pack (pop)

    Arg arg;
    Utils::Copy(arg.m_Receiver, g.m_Remote);
    arg.m_MsgSize = sizeof(arg.m_Msg);
    Utils::Copy(arg.m_Msg, r);

    Env::CallFar_T(g.m_PipeID, arg);

    Env::FundsLock(g.m_Aid, r.m_Amount);

    if (g.m_IsMirror)
        Env::AssetEmit(g.m_Aid, r.m_Amount, 0);
}

export void Method_4(const MirrorCoin::Receive& r)
{
    MirrorCoin::Global g;
    uint8_t gk = 0;
    Env::LoadVar_T(gk, g);

    Env::Halt_if(Utils::IsZero(g.m_Remote));

#pragma pack (push, 1)
    struct Arg
        :public Pipe::VerifyRemote0
    {
        MirrorCoin::Message m_Msg;
    };
#pragma pack (pop)

    Arg arg;
    arg.m_iCheckpoint = r.m_iCheckpoint;
    arg.m_iMsg = r.m_iMsg;
    Utils::Copy(arg.m_Sender, g.m_Remote);
    arg.m_Height = r.m_Height;
    arg.m_Public = 0;
    arg.m_Wipe = 1;

    arg.m_MsgSize = sizeof(arg.m_Msg);
    Utils::Copy(arg.m_Msg, Cast::Down<MirrorCoin::Message>(r));

    Env::CallFar_T(g.m_PipeID, arg);

    if (g.m_IsMirror)
        Env::AssetEmit(g.m_Aid, r.m_Amount, 1);

    Env::FundsUnlock(g.m_Aid, r.m_Amount);
    Env::AddSig(r.m_User);
}

