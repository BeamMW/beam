// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fly_client.h"
#include "../utility/executor.h"

namespace beam {
namespace proto {

FlyClient::NetworkStd::~NetworkStd()
{
    Disconnect();
}

void FlyClient::NetworkStd::Connect()
{
    if (m_Connections.size() == m_Cfg.m_vNodes.size())
    {
        // force (re) connect
        for (ConnectionList::iterator it = m_Connections.begin(); m_Connections.end() != it; ++it)
        {
            Connection& c = *it;
            if (c.IsLive() && c.IsSecureOut())
                continue;

            c.ResetAll();
            if (m_Cfg.m_UseProxy)
                c.Connect(c.m_Addr, m_Cfg.m_ProxyAddr);
            else
                c.Connect(c.m_Addr);
        }
    }
    else
    {
        Disconnect();

        for (size_t i = 0; i < m_Cfg.m_vNodes.size(); i++)
        {
            Connection* pConn = new Connection(*this);
            pConn->m_Addr = m_Cfg.m_vNodes[i];
            if (m_Cfg.m_UseProxy)
                pConn->Connect(pConn->m_Addr, m_Cfg.m_ProxyAddr);
            else
                pConn->Connect(pConn->m_Addr);
        }
    }
}

void FlyClient::NetworkStd::Disconnect()
{
    while (!m_Connections.empty())
        delete &m_Connections.front();
}

FlyClient::NetworkStd::Connection* FlyClient::NetworkStd::get_ActiveConnection()
{
    Connection* pRet = nullptr;

    for (ConnectionList::iterator it = m_Connections.begin(); m_Connections.end() != it; ++it)
    {
        Connection& c = *it;
        if (!c.IsLive() || !c.IsAtTip())
            continue;

        if (pRet)
        {
            if (pRet->m_Dependent.m_vec.size() >= c.m_Dependent.m_vec.size())
                continue;
        }

        pRet = &c;
    }

    return pRet;
}

void FlyClient::NetworkStd::DependentSubscribe(bool bSubscribe)
{
    bool b0 = HasDependentSubscriptions();

    if (bSubscribe)
        m_DependentSubscriptions++;
    else
    {
        if (m_DependentSubscriptions)
            m_DependentSubscriptions--;
    }

    if (HasDependentSubscriptions() != b0)
        OnDependentSubscriptionChanged();
}

void FlyClient::NetworkStd::OnDependentSubscriptionChanged()
{
    for (ConnectionList::iterator it = m_Connections.begin(); m_Connections.end() != it; ++it)
    {
        Connection& c = *it;
        if (c.IsLoginSent())
            c.SendLoginPlus(); // i.e. resend
    }
}

const Merkle::Hash* FlyClient::NetworkStd::get_DependentState(uint32_t& nCount)
{
    nCount = 0;

    auto* pC = get_ActiveConnection();
    if (!pC)
        return nullptr;

    const auto& v = pC->m_Dependent.m_vec;
    nCount = (uint32_t) v.size();
    return nCount ? &v.front() : nullptr;
}

FlyClient::NetworkStd::Connection::Connection(NetworkStd& x)
    : m_This(x)
{
    m_This.m_Connections.push_back(*this);
    ResetVars();
}

FlyClient::NetworkStd::Connection::~Connection()
{
    ResetInternal();
    m_This.m_Connections.erase(ConnectionList::s_iterator_to(*this));

}

bool FlyClient::NetworkStd::Connection::ShouldSync() const
{
    Block::SystemState::Full sTip;
    return !m_This.m_Client.get_History().get_Tip(sTip) || (sTip.m_ChainWork < m_Tip.m_ChainWork);
}

void FlyClient::NetworkStd::Connection::ResetVars()
{
    ZeroObject(m_Tip);
    m_Flags = 0;
    m_NodeID = Zero;

    m_Dependent.m_pQuery.reset();
    m_Dependent.m_vec.clear();
}

void FlyClient::NetworkStd::Connection::ResetInternal()
{
    m_pSync.reset();
	KillTimer();

    if (Flags::Owned & m_Flags)
        m_This.m_Client.OnOwnedNode(m_NodeID, false);

    if (Flags::ReportedConnected & m_Flags)
        m_This.OnNodeConnected(false);

    while (!m_lst.empty())
    {
        RequestNode& n = m_lst.front();
        m_lst.pop_front();
        m_This.m_lst.push_back(n);
    }
}

void FlyClient::NetworkStd::Connection::SendLoginPlus()
{
    SendLogin();

    if (!m_This.HasDependentSubscriptions())
        return;
    if (Flags::DependentPending & m_Flags)
        return;

    Send(proto::Ping());
    m_Flags |= Flags::DependentPending;
}

void FlyClient::NetworkStd::Connection::OnConnectedSecure()
{
	SendLoginPlus();

    if (!(Flags::ReportedConnected & m_Flags))
    {
        m_Flags |= Flags::ReportedConnected;
        m_This.OnNodeConnected(true);
    }
}

void FlyClient::NetworkStd::Connection::SetupLogin(Login& msg)
{
    msg.m_Flags |= LoginFlags::SendPeers;

    if (m_This.m_Cfg.m_PreferOnlineMining)
        msg.m_Flags |= LoginFlags::MiningFinalization;
    if (m_This.HasDependentSubscriptions())
        msg.m_Flags |= LoginFlags::WantDependentState;

    m_This.OnLoginSetup(msg);
}

void FlyClient::NetworkStd::Connection::OnDisconnect(const DisconnectReason& dr)
{
    m_This.OnConnectionFailed(dr);
	ResetAll();
    SetTimer(m_This.m_Cfg.m_ReconnectTimeout_ms);
}

void FlyClient::NetworkStd::Connection::ResetAll()
{
	NodeConnection::Reset();
	ResetInternal();
	ResetVars();
}

void FlyClient::NetworkStd::Connection::SetTimer(uint32_t timeout_ms)
{
    if (!m_pTimer)
        m_pTimer = io::Timer::create(io::Reactor::get_Current());

    m_pTimer->start(timeout_ms, false, [this]() { OnTimer(); });
}

void FlyClient::NetworkStd::Connection::KillTimer()
{
    if (m_pTimer)
        m_pTimer->cancel();
}

void FlyClient::NetworkStd::Connection::OnTimer()
{
    if (IsLive())
    {
        if (m_This.m_Cfg.m_PollPeriod_ms)
        {
            ResetAll();
            uint32_t timeout_ms = std::max(Rules::get().DA.Target_ms, m_This.m_Cfg.m_PollPeriod_ms);
            SetTimer(timeout_ms);
        }
    }
    else
    {
        ResetAll();
        if (m_This.m_Cfg.m_UseProxy) Connect(m_Addr, m_This.m_Cfg.m_ProxyAddr);
        else Connect(m_Addr);
    }
}

void FlyClient::NetworkStd::Connection::OnMsg(Authentication&& msg)
{
    NodeConnection::OnMsg(std::move(msg));

    switch (msg.m_IDType)
    {
    case IDType::Node:
        {
            if (Flags::Node & m_Flags)
                ThrowUnexpected();

            m_Flags |= Flags::Node;
            m_NodeID = msg.m_ID;

            Key::IKdf::Ptr pKdf;
            m_This.m_Client.get_Kdf(pKdf);
            if (pKdf)
            {
                ProveKdfObscured(*pKdf, IDType::Owner);
            }
            else
            {
                Key::IPKdf::Ptr ownerKdf;
                m_This.m_Client.get_OwnerKdf(ownerKdf);
                if (ownerKdf)
                {
                    ProvePKdfObscured(*ownerKdf, IDType::Viewer);
                }
            }
        }
        break;

    case IDType::Viewer:
        {
            if ((Flags::Owned & m_Flags) || !(Flags::Node & m_Flags))
                ThrowUnexpected();

            Key::IPKdf::Ptr pubKdf;
            m_This.m_Client.get_OwnerKdf(pubKdf);
            if (!(pubKdf && IsPKdfObscured(*pubKdf, msg.m_ID)))
                ThrowUnexpected();

            //  viewer confirmed!
            m_Flags |= Flags::Owned;
            m_This.m_Client.OnOwnedNode(m_NodeID, true);
        }
        break;

    default: // suppress warning
        break;
    }
}

void FlyClient::NetworkStd::Connection::OnMsg(GetBlockFinalization&& msg)
{
    if (!(Flags::Owned & m_Flags))
        ThrowUnexpected();

    Key::IKdf::Ptr pKdf;
    m_This.m_Client.get_Kdf(pKdf);
    if (!pKdf)
        ThrowUnexpected(); // ?!

    Key::IPKdf::Ptr pOwner = pKdf;

    Key::Index iIdx = 0;
    if (CoinID(Zero).get_ChildKdfIndex(iIdx))
        pKdf = MasterKey::get_Child(*pKdf, iIdx);

    Block::Builder bb(iIdx, *pKdf, *pOwner, msg.m_Height);
    bb.AddCoinbaseAndKrn();
    bb.AddFees(msg.m_Fees); // TODO: aidMax

    proto::BlockFinalization msgOut;
    msgOut.m_Value.reset(new Transaction);
    bb.m_Txv.MoveInto(*msgOut.m_Value);
    msgOut.m_Value->m_Offset = -bb.m_Offset;
    msgOut.m_Value->Normalize();

    Send(msgOut);
}

void FlyClient::NetworkStd::Connection::OnLogin(Login&& msg, uint32_t /* nFlagsPrev */)
{
    AssignRequests();

    if (LoginFlags::Bbs & m_LoginFlags)
        for (BbsSubscriptions::const_iterator it = m_This.m_BbsSubscriptions.begin(); m_This.m_BbsSubscriptions.end() != it; ++it)
        {
            proto::BbsSubscribe msgOut;
            msgOut.m_TimeFrom = it->second.second;
            msgOut.m_Channel = it->first;
            msgOut.m_On = true;
            Send(msgOut);
        }
}

void FlyClient::NetworkStd::Connection::OnMsg(NewTip&& msg)
{
	if (!msg.m_Description.m_Number.v)
		return; // ignore

    if (m_Tip == msg.m_Description)
        return; // redundant msg

    if (msg.m_Description.m_ChainWork <= m_Tip.m_ChainWork)
        ThrowUnexpected();

    if (!(msg.m_Description.IsValid()))
        ThrowUnexpected();

    m_Dependent.m_vec.clear();

    if (m_pSync && m_pSync->m_vConfirming.empty() && !m_pSync->m_TipBeforeGap.m_Number.v && !m_Tip.IsNext(msg.m_Description))
        m_pSync->m_TipBeforeGap = m_Tip;

    m_Tip = msg.m_Description;

    if (!m_pSync)
    {
        if (ShouldSync())
        {
            StartSync();
        }
        else
        {
            m_This.m_Client.OnTipUnchanged();
            AssignRequests();
        }
    }
}

void FlyClient::NetworkStd::Connection::StartSync()
{
    assert(ShouldSync());
    KillTimer();

    Block::SystemState::Full sTip;
    m_This.m_Client.get_History().get_Tip(sTip);
    if (sTip.IsNext(m_Tip))
    {
        // simple case
        m_This.m_Client.get_History().AddStates(&m_Tip, 1);
        PrioritizeSelf();
        AssignRequests();
        m_This.m_Client.OnNewTip();
    }
    else
    {
        // starting search
        m_pSync.reset(new SyncCtx);
        m_pSync->m_LowHeight = m_Tip.get_Height();
        SearchBelow(m_Tip.get_Height(), 1);
    }
}

void FlyClient::NetworkStd::Connection::SearchBelow(Height h, uint32_t nCount)
{
    assert(ShouldSync() && m_pSync && m_pSync->m_vConfirming.empty());
    assert(nCount);

    struct Walker :public Block::SystemState::IHistory::IWalker
    {
        std::vector<Block::SystemState::Full> m_vStates;
        uint32_t m_Count;

        virtual bool OnState(const Block::SystemState::Full& s) override
        {
            m_vStates.push_back(s);
            return m_vStates.size() < m_Count;
        }
    } w;

    w.m_Count = nCount;
    w.m_vStates.reserve(nCount);
    m_This.m_Client.get_History().Enum(w, &h);

    if (w.m_vStates.empty())
    {
        ZeroObject(m_pSync->m_Confirmed);
        RequestChainworkProof();
    }
    else
    {
        GetCommonState msg;
        msg.m_IDs.resize(w.m_vStates.size());

        for (size_t i = 0; i < msg.m_IDs.size(); i++)
            w.m_vStates[i].get_ID(msg.m_IDs[i]);

        Send(msg);

        m_pSync->m_vConfirming.swap(w.m_vStates);
    }
}

void FlyClient::NetworkStd::Connection::OnMsg(ProofCommonState&& msg)
{
    if (!m_pSync)
        ThrowUnexpected();

    std::vector<Block::SystemState::Full> vStates = std::move(m_pSync->m_vConfirming);
    if (vStates.empty())
        ThrowUnexpected();

    if (!ShouldSync())
    {
        m_pSync.reset();
        return; // other connection was faster
    }

    size_t iState;
    for (iState = 0; ; iState++)
    {
        if (vStates.size() == iState)
        {
            // not found. Theoretically it's possible that the current tip is lower than the requested range (but highly unlikely)
            if (m_Tip.m_Number.v > vStates.back().m_Number.v)
                ThrowUnexpected();

            SearchBelow(m_Tip.get_Height(), 1); // restart
            return;

        }
        if (vStates[iState].m_Number.v == msg.m_ID.m_Number.v)
            break;
    }

    if (!m_Tip.IsValidProofState(msg.m_ID, msg.m_Proof))
        ThrowUnexpected();

    if ((m_pSync->m_LowHeight < vStates.front().get_Height()) && iState)
        SearchBelow(m_pSync->m_LowHeight + 1, 1); // restart the search from this height
    else
    {
        const Block::SystemState::Full& s = vStates[iState];
        Merkle::Hash hv;
        s.get_Hash(hv);
        if (hv != msg.m_ID.m_Hash)
        {
            if (iState != vStates.size() - 1)
                ThrowUnexpected(); // the disproof should have been for the last requested state

            SearchBelow(vStates.back().get_Height(), static_cast<uint32_t>(vStates.size() * 2)); // all the range disproven. Search below
        }
        else
        {
            m_pSync->m_Confirmed = s;
            RequestChainworkProof();
        }
    }
}

struct FlyClient::NetworkStd::Connection::StateArray
{
    std::vector<Block::SystemState::Full> m_vec;

    bool Find(const Block::SystemState::Full&) const;
};

bool FlyClient::NetworkStd::Connection::StateArray::Find(const Block::SystemState::Full& s) const
{
    struct Cmp {
        bool operator () (const Block::SystemState::Full& s, Block::Number num) const { return s.m_Number.v < num.v; }
    };

    // the array should be sorted (this is verified by chainworkproof verification)
    std::vector<Block::SystemState::Full>::const_iterator it = std::lower_bound(m_vec.begin(), m_vec.end(), s.m_Number, Cmp());
    return (m_vec.end() != it) && (*it == s);
}

void FlyClient::NetworkStd::Connection::RequestChainworkProof()
{
    assert(ShouldSync() && m_pSync && m_pSync->m_vConfirming.empty());

    if (Flags::Owned & m_Flags)
    {
        // for trusted nodes this is not required. Go straight to finish
        SyncCtx::Ptr pSync = std::move(m_pSync);
        StateArray arr;
        PostChainworkProof(arr, pSync->m_Confirmed.get_Height());
    }
    else
    {
        GetProofChainWork msg;
        msg.m_LowerBound = m_pSync->m_Confirmed.m_ChainWork;
        Send(msg);

        m_pSync->m_TipBeforeGap.m_Number.v = 0;
        m_pSync->m_LowHeight = m_pSync->m_Confirmed.get_Height();
    }
}

void FlyClient::NetworkStd::Connection::OnMsg(ProofChainWork&& msg)
{
    if (!m_pSync || !m_pSync->m_vConfirming.empty())
        ThrowUnexpected();

    if (msg.m_Proof.m_LowerBound != m_pSync->m_Confirmed.m_ChainWork)
        ThrowUnexpected();

    Block::SystemState::Full sTip;
    if (!msg.m_Proof.IsValid(&sTip))
        ThrowUnexpected();

    if (sTip != m_Tip)
        ThrowUnexpected();

    SyncCtx::Ptr pSync = std::move(m_pSync);

    if (!ShouldSync())
        return;

    // Unpack the proof, convert it to one sorted array. For convenience
    StateArray arr;
	msg.m_Proof.UnpackStates(arr.m_vec);

    if (pSync->m_TipBeforeGap.m_Number.v && pSync->m_Confirmed.m_Number.v)
    {
        // Since there was a gap in the tips reported by the node (which is typical in case of reorgs) - there is a possibility that our m_Confirmed is no longer valid.
        // If either the m_Confirmed or the m_TipBeforeGap are mentioned in the chainworkproof - then there's no problem with reorg.
        // And since chainworkproof usually contains a "tail" of consecutive headers - there should be no problem, unless the reorg is huge
        // Otherwise sync should be repeated
        if (!arr.Find(pSync->m_TipBeforeGap) &&
            !arr.Find(pSync->m_Confirmed))
        {
            StartSync(); // again
            return;
        }
    }

    PostChainworkProof(arr, pSync->m_LowHeight);
}

void FlyClient::NetworkStd::Connection::PostChainworkProof(const StateArray& arr, Height hLowHeight)
{
    struct Walker :public Block::SystemState::IHistory::IWalker
    {
        Height m_LowHeight;
        Height m_LowErase;
        const StateArray* m_pArr;

        virtual bool OnState(const Block::SystemState::Full& s) override
        {
            if (s.get_Height() <= m_LowHeight)
                return false;

            if (m_pArr->Find(s))
                return false;

            m_LowErase = s.get_Height();
            return true;
        }
    } w;

    w.m_LowErase = MaxHeight;
    w.m_LowHeight = hLowHeight;
    w.m_pArr = &arr;

    m_This.m_Client.get_History().Enum(w, NULL);

    if (w.m_LowErase != MaxHeight)
    {
        m_This.m_Client.get_History().DeleteFrom(w.m_LowErase);

        // if more connections are opened simultaneously - notify them
        for (ConnectionList::iterator it = m_This.m_Connections.begin(); m_This.m_Connections.end() != it; ++it)
        {
            const Connection& c = *it;
            if (c.m_pSync)
                std::setmin(c.m_pSync->m_LowHeight, w.m_LowErase - 1);
        }

        m_This.m_Client.OnRolledBack();
    }

    if (arr.m_vec.empty())
        m_This.m_Client.get_History().AddStates(&m_Tip, 1);
    else
        m_This.m_Client.get_History().AddStates(&arr.m_vec.front(), arr.m_vec.size());
    PrioritizeSelf();
    m_This.m_Client.OnNewTip(); // finished!
    AssignRequests();
}


void FlyClient::NetworkStd::Connection::PrioritizeSelf()
{
    m_This.m_Connections.erase(ConnectionList::s_iterator_to(*this));
    m_This.m_Connections.push_front(*this);
}

void FlyClient::INetwork::PostRequest(Request& r, Request::IHandler& h)
{
    assert(!r.m_pTrg);
    r.m_pTrg = &h;
    PostRequestInternal(r);
}

void FlyClient::NetworkStd::PostRequestInternal(Request& r)
{
    assert(r.m_pTrg);

    if (Request::Type::BbsMsg == r.get_Type())
    {
        auto& r2 = Cast::Up<RequestBbsMsg>(r);
        if ((Request::Type::BbsMsg == r.get_Type()) && (Rules::get().m_Consensus != Rules::Consensus::FakePoW))
        {
            m_BbsMiner.Post(r2);
            return;
        }

        r2.m_Msg.m_TimePosted = getTimestamp();
    }

    RequestNode* pNode = m_lst.Create_back();
    pNode->m_pRequest = &r;

    OnNewRequests();
}

void FlyClient::NetworkStd::OnNewRequests()
{
    for (ConnectionList::iterator it = m_Connections.begin(); m_Connections.end() != it; ++it)
    {
        Connection& c = *it;
        if (c.IsLive() && c.IsSecureOut())
        {
            c.AssignRequests();
            break;
        }
    }
}

void FlyClient::NetworkStd::Connection::OnMsg(DependentContextChanged&& msg)
{
    if (msg.m_vCtxs.empty())
        ThrowUnexpected();

    if (msg.m_PrefixDepth)
    {
        if (m_Dependent.m_vec.size() < msg.m_PrefixDepth)
            ThrowUnexpected();

        m_Dependent.m_vec.resize(msg.m_PrefixDepth + msg.m_vCtxs.size());

        for (size_t i = 0; i < msg.m_vCtxs.size(); i++)
            m_Dependent.m_vec[i + msg.m_PrefixDepth] = msg.m_vCtxs[i];
    }
    else
        m_Dependent.m_vec.swap(msg.m_vCtxs);

    if (this == m_This.get_ActiveConnection())
        m_This.m_Client.OnDependentStateChanged();
}

bool FlyClient::NetworkStd::Connection::IsAtTip() const
{
    Block::SystemState::Full sTip;
    return m_This.m_Client.get_History().get_Tip(sTip) && (sTip == m_Tip);
}

void FlyClient::NetworkStd::Connection::AssignRequests()
{
    if (!(Flags::Node & m_Flags))
        return;

    // currently we have no requests that can be assigned unless at tip.
    if (!IsAtTip())
        return;

    RequestList lst;
    for (lst.swap(m_This.m_lst); !lst.empty(); )
    {
        RequestNode& n = lst.front();
        if (n.m_pRequest->m_pTrg)
        {
            lst.pop_front();
            m_lst.push_back(n);
            AssignRequest(n);
        }
        else
            lst.Delete(n);
    }

    if (m_lst.empty() && m_This.m_Cfg.m_PollPeriod_ms)
        SetTimer(m_This.m_Cfg.m_CloseConnectionDelay_ms); // this should allow to get sbbs messages
    else
        KillTimer();
}

void FlyClient::NetworkStd::Connection::AssignRequest(RequestNode& n)
{
    assert(n.m_pRequest);

    switch (n.m_pRequest->get_Type())
    {
#define THE_MACRO(type) \
    case Request::Type::type: \
        if (SendRequest(Cast::Up<Request##type>(*n.m_pRequest))) \
            return; \
        break;

    REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO

    default: // ?!
        m_lst.Finish(n);
        return;
    }

    // could not assign the request, return it to global list
    m_lst.erase(RequestList::s_iterator_to(n));
    m_This.m_lst.push_back(n);
}

void FlyClient::NetworkStd::RequestList::Finish(RequestNode& n)
{
    assert(n.m_pRequest);
    if (n.m_pRequest->m_pTrg)
        n.m_pRequest->m_pTrg->OnComplete(*n.m_pRequest);
    Delete(n);
}

FlyClient::NetworkStd::RequestNode& FlyClient::NetworkStd::Connection::get_FirstRequest()
{
    if (m_lst.empty())
        ThrowUnexpected();
    RequestNode& n = m_lst.front();
    assert(n.m_pRequest);

    return n;
}

#define REQUEST_STD_RCV(type, msgIn) \
void FlyClient::NetworkStd::Connection::OnMsg(msgIn&& msg) \
{ \
    auto& n = get_FirstRequest(); \
    auto& r = n.m_pRequest->As<Request##type>(); \
    r.m_Res = std::move(msg); \
 \
     OnRequestData(r); \
 \
    OnDone(n); \
}


#define THE_MACRO(type, msgOut, msgIn) \
REQUEST_STD_RCV(type, msgIn) \
 \
bool FlyClient::NetworkStd::Connection::SendRequest(Request##type& req) \
{  \
    if (!IsSupported(req)) \
        return false; \
 \
    Send(req.m_Msg); \
    return true; \
}

REQUEST_TYPES_Std(THE_MACRO)
#undef THE_MACRO

void FlyClient::NetworkStd::Connection::OnRequestData(RequestUtxo& req)
{
    for (size_t i = 0; i < req.m_Res.m_Proofs.size(); i++)
        if (!m_Tip.IsValidProofUtxo(req.m_Msg.m_Utxo, req.m_Res.m_Proofs[i]))
            ThrowUnexpected();
}

void FlyClient::NetworkStd::Connection::OnRequestData(RequestKernel& req)
{
    if (!req.m_Res.m_Proof.empty())
        if (!m_Tip.IsValidProofKernel(req.m_Msg.m_ID, req.m_Res.m_Proof))
            ThrowUnexpected();
}

void FlyClient::NetworkStd::Connection::OnRequestData(RequestAsset& req)
{
    if (req.m_Res.m_Info.m_Owner != Zero) // valid asset info
    {
        if (req.m_Msg.m_Owner != Zero && req.m_Msg.m_Owner != req.m_Res.m_Info.m_Owner)
            ThrowUnexpected();

        if (req.m_Msg.m_AssetID && (req.m_Msg.m_AssetID != req.m_Res.m_Info.m_ID))
            ThrowUnexpected();
    }

    if (!req.m_Res.m_Proof.empty())
        if (!m_Tip.IsValidProofAsset(req.m_Res.m_Info, req.m_Res.m_Proof))
            ThrowUnexpected();
}

bool FlyClient::NetworkStd::Connection::IsSupported(RequestEvents&)
{
    return !!(Flags::Owned & m_Flags);
}

bool FlyClient::NetworkStd::Connection::SendRequest(RequestEnsureSync& req)
{
    if (req.m_IsDependent)
    {
        if (!m_This.HasDependentSubscriptions())
        {
            // temporarily emulate subscription, to get the most recent dependent state
            {
                uint32_t nSub = 1;
                TemporarySwap ts(nSub, m_This.m_DependentSubscriptions);

                SendLoginPlus();
            }
            SendLogin();
        }

        if (Flags::DependentPending & m_Flags)
            return true;
    }

    RequestNode& n = m_lst.back(); // SendRequest is called on the most recently added request
    assert(&req == n.m_pRequest);

    OnDone(n);

    return true;
}

bool FlyClient::NetworkStd::Connection::IsSupported(RequestTransaction& req)
{
    if (!(LoginFlags::SpreadingTransactions & m_LoginFlags))
        return false;

    if (req.m_Msg.m_Context && (get_Ext() < 9))
        return false;

    return true;
}

void FlyClient::NetworkStd::Connection::OnRequestData(RequestProofShieldedInp& req)
{
    if (!req.m_Res.m_Proof.empty())
    {
        ShieldedTxo::DescriptionInp desc;
        desc.m_Height = req.m_Res.m_Height;
        desc.m_SpendPk = req.m_Msg.m_SpendPk;

        if (!m_Tip.IsValidProofShieldedInp(desc, req.m_Res.m_Proof))
            ThrowUnexpected();
    }
}

void FlyClient::NetworkStd::Connection::OnRequestData(RequestProofShieldedOutp& req)
{
    if (!req.m_Res.m_Proof.empty())
    {
        ShieldedTxo::DescriptionOutp desc;
        desc.m_ID = req.m_Res.m_ID;
        desc.m_Height = req.m_Res.m_Height;
        desc.m_SerialPub = req.m_Msg.m_SerialPub;
        desc.m_Commitment = req.m_Res.m_Commitment;

        if (!m_Tip.IsValidProofShieldedOutp(desc, req.m_Res.m_Proof))
            ThrowUnexpected();
    }
}

bool FlyClient::Data::DecodedHdrPack::DecodeAndCheck(const HdrPack& msg)
{
    if (msg.m_vElements.empty())
        return true; // this is allowed

    // PoW verification is heavy for big packs. Do it in parallel
    std::vector<Block::SystemState::Full> v;
    v.resize(msg.m_vElements.size());

    Cast::Down<Block::SystemState::Sequence::Prefix>(v.front()) = msg.m_Prefix;
    Cast::Down<Block::SystemState::Sequence::Element>(v.front()) = msg.m_vElements.back();

    for (size_t i = 1; i < msg.m_vElements.size(); i++)
    {
        Block::SystemState::Full& s0 = v[i - 1];
        Block::SystemState::Full& s1 = v[i];

        s0.get_Hash(s1.m_Prev);
        s1.m_Height = s0.m_Height + 1;
        Cast::Down<Block::SystemState::Sequence::Element>(s1) = msg.m_vElements[msg.m_vElements.size() - i - 1];
        s1.m_ChainWork = s0.m_ChainWork + s1.m_PoW.m_Difficulty;
    }

    struct MyTask
        :public Executor::TaskSync
    {
        const Block::SystemState::Full* m_pV;
        uint32_t m_Count;
        bool m_Valid;

        virtual ~MyTask() {}

        virtual void Exec(Executor::Context& ctx) override
        {
            uint32_t i0, nCount;
            ctx.get_Portion(i0, nCount, m_Count);
            TestRange(i0, nCount);
        }

        void TestRange(uint32_t i0, uint32_t nCount)
        {
            nCount += i0;
            for (; i0 < nCount; i0++)
                if (!m_pV[i0].IsValid())
                    m_Valid = false;
        }
    };

    MyTask t;
    t.m_pV = &v.front();
    t.m_Count = static_cast<uint32_t>(v.size());
    t.m_Valid = true;

    if (Executor::s_pInstance)
        Executor::s_pInstance->ExecAll(t);
    else
        t.TestRange(0, t.m_Count);

    if (t.m_Valid)
        m_vStates = std::move(v);

    return t.m_Valid;
}

bool FlyClient::NetworkStd::Connection::SendRequest(RequestEnumHdrs& req)
{
    Send(req.m_Msg);
    return true;
}

void FlyClient::NetworkStd::Connection::OnMsg(proto::HdrPack&& msg)
{
    auto& n = get_FirstRequest();
    auto& r = n.m_pRequest->As<RequestEnumHdrs>();

    if (!r.DecodeAndCheck(msg))
        ThrowUnexpected();

    OnDone(n, r.m_vStates.empty());
}

void FlyClient::NetworkStd::Connection::OnMsg(DataMissing&& msg)
{
    auto& n = get_FirstRequest();
    auto& r = *n.m_pRequest;

    switch (r.get_Type())
    {
    case Request::Type::EnumHdrs:
    case Request::Type::BodyPack:
        OnDone(n);
        break;

    default:
        ThrowUnexpected();
    }
}

bool FlyClient::NetworkStd::Connection::SendRequest(RequestKernel2& req)
{
    Send(req.m_Msg);
    return true;
}

bool FlyClient::NetworkStd::Connection::SendRequest(RequestKernel3& req)
{
    if (get_Ext() < 11)
        return false;

    Send(req.m_Msg);
    return true;
}

void FlyClient::NetworkStd::Connection::OnMsg(ProofKernel2&& msg)
{
    auto& n = get_FirstRequest();
    switch (n.m_pRequest->get_Type())
    {
    case Request::Type::Kernel2:
        {
            auto& req = Cast::Up<RequestKernel2>(*n.m_pRequest);
            req.m_Res = std::move(msg);

            if (req.m_Res.m_Proof.empty())
            {
                if (req.m_Res.m_Kernel)
                    ThrowUnexpected();
            }
            else
            {
                if (!m_Tip.IsValidProofKernel(req.m_Msg.m_ID, req.m_Res.m_Proof))
                    ThrowUnexpected();

                if (req.m_Res.m_Kernel)
                {
                    if (!req.m_Msg.m_Fetch)
                        ThrowUnexpected();

                    if (req.m_Res.m_Kernel->IsValid(req.m_Res.m_Height))
                        ThrowUnexpected();

                    if (req.m_Res.m_Kernel->get_ID() != req.m_Msg.m_ID)
                        ThrowUnexpected();
                }
                else
                {
                    if (req.m_Msg.m_Fetch)
                        ThrowUnexpected();
                }
            }
        }
        break;

    case Request::Type::Kernel3:
        {
            auto& req = Cast::Up<RequestKernel3>(*n.m_pRequest);
            req.m_Res = std::move(msg);

            if (req.m_Res.m_Kernel)
            {
                if (!req.m_Res.m_Kernel->IsValid(req.m_Res.m_Height))
                    ThrowUnexpected();

                if (req.m_Msg.m_WithProof && !m_Tip.IsValidProofKernel(req.m_Res.m_Kernel->get_ID(), req.m_Res.m_Proof))
                    ThrowUnexpected();
            }

        }
        break;

    default:
        proto::NodeConnection::ThrowUnexpected();
    }
 
    OnDone(n);
}

bool FlyClient::NetworkStd::Connection::SendTrgCtx(const std::unique_ptr<Merkle::Hash>& pCtx)
{
    if (pCtx)
    {
        if (get_Ext() < 9)
            return false;

        if (m_Dependent.m_pQuery)
        {
            if (*pCtx == *m_Dependent.m_pQuery)
                return true;
        }
        else
            m_Dependent.m_pQuery = std::make_unique<Merkle::Hash>();

        *m_Dependent.m_pQuery = *pCtx;
    }
    else
    {
        if (!m_Dependent.m_pQuery)
            return true;
        m_Dependent.m_pQuery.reset();
    }

    proto::SetDependentContext msg;
    TemporarySwap ts(msg.m_Context, m_Dependent.m_pQuery);
    Send(msg);

    return true;
}

bool FlyClient::NetworkStd::Connection::SendRequest(RequestContractVars& req)
{
    if (!SendTrgCtx(req.m_pCtx))
        return false;

    Send(req.m_Msg);
    return true;
}

REQUEST_STD_RCV(ContractVars, ContractVars)

bool FlyClient::NetworkStd::Connection::SendRequest(RequestContractLogs& req)
{
    if (!SendTrgCtx(req.m_pCtx))
        return false;

    Send(req.m_Msg);
    return true;
}

REQUEST_STD_RCV(ContractLogs, ContractLogs)

void FlyClient::NetworkStd::Connection::OnRequestData(RequestContractVar& req)
{
    if (!req.m_Res.m_Proof.empty() && !m_Tip.IsValidProofContract(req.m_Msg.m_Key, req.m_Res.m_Value, req.m_Res.m_Proof))
        ThrowUnexpected();
}

bool FlyClient::NetworkStd::Connection::SendRequest(RequestBbsMsg& req)
{
    if (!(LoginFlags::Bbs & m_LoginFlags))
        return false;

    Send(req.m_Msg);
    Send(proto::Ping());

    return true;
}

bool FlyClient::NetworkStd::Connection::SendRequest(RequestAssetsListAt& req)
{
    if (get_Ext() < 10)
        return false;

    Send(req.m_Msg);
    return true;
}

void FlyClient::NetworkStd::Connection::OnMsg(proto::Pong&&)
{
    if (!m_lst.empty())
    {
        auto& n = get_FirstRequest();
        switch (n.m_pRequest->get_Type())
        {
        case Request::Type::BbsMsg:
            OnDone(n, false);
            return;

        default: // suppress warning
            break;
        }
    }

    if (!(Flags::DependentPending & m_Flags))
        ThrowUnexpected();
    m_Flags &= ~Flags::DependentPending;

    for (auto it = m_lst.begin(); m_lst.end() != it; )
    {
        auto& n = *it++;
        if (Request::Type::EnsureSync == n.m_pRequest->get_Type())
            OnDone(n, false);
    }
}

void FlyClient::NetworkStd::Connection::OnMsg(proto::AssetsListAt&& msg)
{
    auto& n = get_FirstRequest();
    auto& r = n.m_pRequest->As<RequestAssetsListAt>();

    if (msg.m_Assets.empty())
    {
        if (msg.m_bMore)
            ThrowUnexpected();
    }
    else
    {
        Asset::ID aid0 = r.m_Msg.m_Aid0;
        for (const auto& ai : msg.m_Assets)
        {
            if (aid0 > ai.m_ID)
                ThrowUnexpected();

            aid0 = ai.m_ID + 1;
        }

        if (r.m_Res.empty())
            r.m_Res = std::move(msg.m_Assets);
        else
            r.m_Res.insert(r.m_Res.end(), msg.m_Assets.begin(), msg.m_Assets.end());


        r.m_Msg.m_Aid0 = aid0;
    }


    if (msg.m_bMore)
        Send(r.m_Msg);
    else
        OnDone(n);
}

void FlyClient::NetworkStd::Connection::OnDone(RequestNode& n, bool bMaybeRetry /* = true */)
{
    assert(n.m_pRequest);

    if (n.m_pRequest->m_pTrg)
    {
        if (bMaybeRetry && !IsAtTip())
        {
            // should retry
            m_lst.erase(RequestList::s_iterator_to(n));
            m_This.m_lst.push_back(n);
            m_This.OnNewRequests();
            return;
        }

        m_lst.Finish(n);
    }
    else
        m_lst.Delete(n); // aborted already

    if (m_lst.empty() && m_This.m_Cfg.m_PollPeriod_ms)
        SetTimer(0);
}

void FlyClient::NetworkStd::BbsSubscribe(BbsChannel ch, Timestamp ts, IBbsReceiver* p)
{
    BbsSubscriptions::iterator it = m_BbsSubscriptions.find(ch);
    if (m_BbsSubscriptions.end() == it)
    {
        if (!p)
            return;

        m_BbsSubscriptions.emplace(std::make_pair(ch, std::make_pair(p, ts)));
    }
    else
    {
        if (p)
        {
            it->second.first = p;
            it->second.second = ts;
            return;
        }

        m_BbsSubscriptions.erase(it);
    }

    proto::BbsSubscribe msg;
    msg.m_TimeFrom = ts;
    msg.m_Channel = ch;
    msg.m_On = (nullptr != p);

    for (ConnectionList::iterator it2 = m_Connections.begin(); m_Connections.end() != it2; ++it2)
        if (it2->IsLive() && it2->IsSecureOut())
            it2->Send(msg);
}

void FlyClient::NetworkStd::Connection::OnMsg(BbsMsg&& msg)
{
    BbsSubscriptions::iterator it = m_This.m_BbsSubscriptions.find(msg.m_Channel);
    if (m_This.m_BbsSubscriptions.end() != it)
    {
        it->second.second = msg.m_TimePosted;

        assert(it->second.first);
        it->second.first->OnMsg(std::move(msg));
    }
}

void FlyClient::NetworkStd::Connection::OnMsg(EventsSerif&& msg)
{
    if (!(Flags::Owned & m_Flags))
        ThrowUnexpected();

    // TODO: handle complex situation, where multiple owned nodes are connected
    m_This.m_Client.OnEventsSerif(msg.m_Value, msg.m_Height);
}

void FlyClient::NetworkStd::Connection::OnMsg(PeerInfo&& msg)
{
    m_This.m_Client.OnNewPeer(msg.m_ID, msg.m_LastAddr);
}

//////////////
// BbsMiner
void FlyClient::NetworkStd::BbsMiner::Post(RequestBbsMsg& r)
{
    if (!m_pEvtMined)
    {
        m_pEvtMined = io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { OnMined(); });
        m_iCurrent = 1;

#if defined(EMSCRIPTEN)
        uint32_t nThreads = 3;
#else
        uint32_t nThreads = MyThread::hardware_concurrency();
#endif
        nThreads = (nThreads > 1) ? (nThreads - 1) : 1; // leave at least 1 vacant core for other things
        m_vThreads.resize(nThreads);

        for (uint32_t i = 0; i < nThreads; i++)
            m_vThreads[i] = MyThread(&BbsMiner::Thread, this, i);
    }

    auto pNode = std::make_unique<RequestNode>();
    pNode->m_pRequest = &r;

    {
        std::scoped_lock<std::mutex> scope(m_Mutex);
        m_lstToMine.push_back(*pNode.release());
        m_NewTask.notify_all();
    }
}

void FlyClient::NetworkStd::BbsMiner::Stop()
{
    if (!m_vThreads.empty())
    {
        {
            std::unique_lock<std::mutex> scope(m_Mutex);
            m_iCurrent = 0;
            m_NewTask.notify_all();
        }

        for (size_t i = 0; i < m_vThreads.size(); i++)
            if (m_vThreads[i].joinable())
                m_vThreads[i].join();

        m_vThreads.clear();
    }

    m_pEvtMined.reset();
    m_lstToMine.Clear();
    m_lstMined.Clear();
}

void FlyClient::NetworkStd::BbsMiner::Thread(uint32_t iThread)
{
    proto::Bbs::NonceType nStep = static_cast<uint32_t>(m_vThreads.size());

    while (true)
    {
        ECC::Hash::Processor hpPartial(Uninitialized);
        uint64_t iCurrent;

        for (std::unique_lock<std::mutex> scope(m_Mutex); ; m_NewTask.wait(scope))
        {
            if (!m_iCurrent)
                return; // shutdown

            if (!m_lstToMine.empty())
            {
                if (!m_hpPartial.IsInitialized())
                {
                    m_hpPartial.Reset();
                    proto::Bbs::get_HashPartial(m_hpPartial, Cast::Up<RequestBbsMsg>(*m_lstToMine.front().m_pRequest).m_Msg);
                    assert(m_hpPartial.IsInitialized());
                }

                hpPartial = m_hpPartial;
                iCurrent = m_iCurrent;
                break;
            }
        }

        Timestamp ts = 0;
        proto::Bbs::NonceType nonce = iThread;
        bool bSuccess = false;

        for (uint32_t i = 0; ; i++)
        {
            if (m_iCurrent != iCurrent)
                break;

            if (!(i & 0xff))
                ts = getTimestamp();

            // attempt to mine it
            ECC::Hash::Value hv;
            ECC::Hash::Processor hp = hpPartial;
            hp
                << ts
                << nonce
                >> hv;

            if (proto::Bbs::IsHashValid(hv))
            {
                bSuccess = true;
                break;
            }

            nonce += nStep;
        }

        if (bSuccess)
        {
            bool bNotify = false;

            {
                std::unique_lock<std::mutex> scope(m_Mutex);

                if (m_iCurrent == iCurrent)
                {
                    assert(!m_lstToMine.empty());
                    auto& n = m_lstToMine.front();
                    auto& msg = Cast::Up<RequestBbsMsg>(*n.m_pRequest).m_Msg;

                    msg.m_TimePosted = ts;
                    msg.m_Nonce = nonce;

                    if (m_lstMined.empty())
                        bNotify = true;

                    m_lstToMine.pop_front();
                    m_lstMined.push_back(n);

                    m_iCurrent = iCurrent + 1; // advance
                    m_hpPartial.Reset(Uninitialized);
                }
            }

            if (bNotify)
                m_pEvtMined->post();
        }
    }
}

void FlyClient::NetworkStd::BbsMiner::OnMined()
{
    if (!m_lstMined.empty())
    {
        do
        {
            auto& n = m_lstMined.front();
            m_lstMined.pop_front();
            get_ParentObj().m_lst.push_back(n);

        } while (!m_lstMined.empty());

        get_ParentObj().OnNewRequests();
    }
}

} // namespace proto
} // namespace beam
