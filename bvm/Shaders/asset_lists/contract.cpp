#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../upgradable3/contract_impl.h"
#include "../dao-vault/contract.h"

namespace AssetLists {

// ============================================================
// Buffer-size constants
// ============================================================

static const uint32_t s_AccountStrBufMax =
    1 + Account::s_NameMaxLen +
    1 + Account::s_WebsiteMaxLen +
    2 + Account::s_DescMaxLen;

static const uint32_t s_ListStrBufMax =
    1 + List::s_NameMaxLen +
    2 + List::s_DescMaxLen;

static const uint32_t s_AccountValMax = sizeof(Account) + s_AccountStrBufMax;
static const uint32_t s_ListValMax    = sizeof(List)    + s_ListStrBufMax;

// Largest possible payload per action type:
//   s_UpdateInfo (account) = header + 64 + 128 + 256 = 452
//   s_UpdateInfo (list)    = header + 64 + 256       = 323
//   s_CreateList           = sizeof(PayloadCreateList) + 64 + 256
//   s_SetListManagers      = s_SignerSetMaxValSize
// Use the account UpdateInfo size as the conservative upper bound.
static const uint32_t s_ProposalPayloadMax =
    sizeof(PayloadUpdateAccountInfo) +
    Account::s_NameMaxLen + Account::s_WebsiteMaxLen + Account::s_DescMaxLen;

static const uint32_t s_ProposalValMax = sizeof(Proposal) + s_ProposalPayloadMax;

// ============================================================
// State helper
// ============================================================

struct MyState : public State
{
    MyState() { Env::LoadVar_T((uint8_t) State::s_Key, *this); }
    void Save() { Env::SaveVar_T((uint8_t) State::s_Key, *this); }
};

// ============================================================
// Helpers
// ============================================================

static void SendFee(const ContractID& cid, Amount amount)
{
    if (!amount) return;
    DaoVault::Method::Deposit arg;
    arg.m_Aid    = 0; // BEAM
    arg.m_Amount = amount;
    Env::CallFar_T(cid, arg);
}

// Accumulate BEAM into an account's on-chain balance (from proposal fees)
static void AddAccountBalance(const PubKey& pkAccount, Amount amount)
{
    if (!amount) return;
    AccountBalance::Key bk;
    _POD_(bk.m_pkAccount) = pkAccount;
    AccountBalance bal;
    if (!Env::LoadVar_T(bk, bal))
        bal.m_Amount = 0;
    Strict::Add(bal.m_Amount, amount);
    Env::SaveVar_T(bk, bal);
}

// Count set bits in a uint8_t (popcount)
static uint8_t CountBits(uint8_t v)
{
    uint8_t n = 0;
    for (; v; v &= v - 1) n++;
    return n;
}

// Compare two PubKeys — returns true if equal
static bool PkEq(const PubKey& a, const PubKey& b)
{
    return !Env::Memcmp(&a, &b, sizeof(PubKey));
}

// Load a full variable-length value into buf. Halts if fewer than nMin bytes read.
template <typename TKey>
static uint32_t LoadFull(const TKey& key, void* pBuf, uint32_t nBufMax, uint32_t nMin)
{
    uint32_t n = Env::LoadVar(&key, sizeof(key), pBuf, nBufMax, KeyTag::Internal);
    Env::Halt_if(n < nMin);
    return n;
}

template <typename TKey>
static void SaveFull(const TKey& key, const void* pBuf, uint32_t nBytes)
{
    Env::SaveVar(&key, sizeof(key), pBuf, nBytes, KeyTag::Internal);
}

// ============================================================
// String packing helpers
// ============================================================

static uint32_t PackAccountStrings(uint8_t* dst,
    uint8_t nName, uint8_t nWeb, uint16_t nDesc, const void* pSrc)
{
    Env::Halt_if(nName > Account::s_NameMaxLen);
    Env::Halt_if(nWeb  > Account::s_WebsiteMaxLen);
    Env::Halt_if(nDesc > Account::s_DescMaxLen);

    uint8_t*       p = dst;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(pSrc);

    *p++ = nName;  Env::Memcpy(p, s, nName);  p += nName;  s += nName;
    *p++ = nWeb;   Env::Memcpy(p, s, nWeb);   p += nWeb;   s += nWeb;
    p[0] = (uint8_t) nDesc;       p[1] = (uint8_t)(nDesc >> 8); p += 2;
    Env::Memcpy(p, s, nDesc);     p += nDesc;
    return (uint32_t)(p - dst);
}

static uint32_t PackListStrings(uint8_t* dst,
    uint8_t nName, uint16_t nDesc, const void* pSrc)
{
    Env::Halt_if(nName > List::s_NameMaxLen);
    Env::Halt_if(nDesc > List::s_DescMaxLen);

    uint8_t*       p = dst;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(pSrc);

    *p++ = nName;  Env::Memcpy(p, s, nName);  p += nName;  s += nName;
    p[0] = (uint8_t) nDesc;       p[1] = (uint8_t)(nDesc >> 8); p += 2;
    Env::Memcpy(p, s, nDesc);     p += nDesc;
    return (uint32_t)(p - dst);
}

// ============================================================
// SignerSet helpers
// ============================================================

static uint32_t LoadSigners(const AccountSigners::Key& sk,
    uint8_t (&buf)[s_SignerSetMaxValSize])
{
    return LoadFull(sk, buf, sizeof(buf), sizeof(SignerSet));
}

static uint32_t LoadAccountSigners(const PubKey& pkAccount,
    uint8_t (&buf)[s_SignerSetMaxValSize])
{
    AccountSigners::Key sk;
    _POD_(sk.m_pkAccount) = pkAccount;
    return LoadSigners(sk, buf);
}

// Load the effective signer set for a list:
// uses list-specific managers if present, otherwise falls back to account signers.
static uint32_t LoadListEffectiveSigners(const PubKey& pkAccount, const PubKey& pkList,
    uint8_t (&buf)[s_SignerSetMaxValSize])
{
    ListSigners::Key lsk;
    _POD_(lsk.m_pkAccount) = pkAccount;
    _POD_(lsk.m_pkList)    = pkList;

    uint32_t n = Env::LoadVar(&lsk, sizeof(lsk), buf, sizeof(buf), KeyTag::Internal);
    if (n >= sizeof(SignerSet))
        return n;

    return LoadAccountSigners(pkAccount, buf);
}

// Returns the 0-based index of pk in ss, or ss.m_nSigners if not found.
static uint8_t FindSignerIdx(const SignerSet& ss, const PubKey& pk)
{
    for (uint8_t i = 0; i < ss.m_nSigners; i++)
        if (PkEq(ss.Signers()[i], pk))
            return i;
    return ss.m_nSigners; // not found
}

// ============================================================
// Proposal helpers
// ============================================================

struct VoteResult { bool execute; bool reject; };

// Apply a vote to a proposal. Updates YesMask/NoMask and returns the outcome.
// Halts if expired, or if this signer already voted.
static VoteResult ApplyVote(Proposal& p, const SignerSet& ss,
    uint8_t signerIdx, uint8_t bYes)
{
    Env::Halt_if(Env::get_Height() > p.m_hExpire); // expired
    Env::Halt_if((p.m_YesMask | p.m_NoMask) >> signerIdx & 1); // already voted

    if (bYes)
    {
        p.m_YesMask |= (uint8_t)(1u << signerIdx);
        return { CountBits(p.m_YesMask) >= ss.m_Threshold, false };
    }
    else
    {
        p.m_NoMask |= (uint8_t)(1u << signerIdx);
        return { false, CountBits(p.m_NoMask) >= ss.RejectThreshold() };
    }
}

// Try to auto-cast a yes vote from the proposer if they are a signer.
// Returns the vote outcome so the caller can decide whether to execute immediately.
static VoteResult AutoVoteIfSigner(Proposal& p, const SignerSet& ss,
    const PubKey& pkProposer)
{
    uint8_t idx = FindSignerIdx(ss, pkProposer);
    if (idx < ss.m_nSigners)
        return ApplyVote(p, ss, idx, 1 /*yes*/);
    return { false, false };
}

// ============================================================
// Account action executors
// ============================================================

static void ExecUpdateAccountInfo(const PubKey& pkAccount,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadUpdateAccountInfo));
    const auto& pay = *reinterpret_cast<const PayloadUpdateAccountInfo*>(&p + 1);

    Account::Key ak;
    _POD_(ak.m_pkAccount) = pkAccount;

    uint8_t  buf[s_AccountValMax];
    uint32_t nOld = LoadFull(ak, buf, sizeof(buf), sizeof(Account));
    Account& hdr  = *reinterpret_cast<Account*>(buf);

    uint8_t strBuf[s_AccountStrBufMax];
    uint32_t nStr = PackAccountStrings(strBuf, pay.m_nNameLen, pay.m_nWebsiteLen,
        pay.m_nDescLen, &pay + 1);
    Env::Memcpy(buf + sizeof(Account), strBuf, nStr);
    SaveFull(ak, buf, sizeof(Account) + nStr);
    (void) nOld; (void) hdr;
}

static void ExecDeleteAccount(const PubKey& pkAccount)
{
    Account::Key ak;
    _POD_(ak.m_pkAccount) = pkAccount;
    Env::Halt_if(!Env::DelVar_T(ak));

    AccountSigners::Key sk;
    _POD_(sk.m_pkAccount) = pkAccount;
    Env::DelVar_T(sk);

    MyState s;
    s.m_nAccounts--;
    s.Save();
}

static void ExecAddSigner(const PubKey& pkAccount,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadAddSigner));
    const auto& pay = *reinterpret_cast<const PayloadAddSigner*>(&p + 1);

    AccountSigners::Key sk;
    _POD_(sk.m_pkAccount) = pkAccount;

    uint8_t buf[s_SignerSetMaxValSize];
    LoadFull(sk, buf, sizeof(buf), sizeof(SignerSet));
    SignerSet& ss = *reinterpret_cast<SignerSet*>(buf);

    Env::Halt_if(ss.m_nSigners >= s_MaxSigners);
    _POD_(ss.Signers()[ss.m_nSigners]) = pay.m_pkNew;
    ss.m_nSigners++;
    ss.m_Threshold = pay.m_NewThreshold;
    Env::Halt_if(!ss.IsValid());
    SaveFull(sk, buf, ss.ValSize());
}

static void ExecRemoveSigner(const PubKey& pkAccount,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadRemoveSigner));
    const auto& pay = *reinterpret_cast<const PayloadRemoveSigner*>(&p + 1);

    AccountSigners::Key sk;
    _POD_(sk.m_pkAccount) = pkAccount;

    uint8_t buf[s_SignerSetMaxValSize];
    LoadFull(sk, buf, sizeof(buf), sizeof(SignerSet));
    SignerSet& ss = *reinterpret_cast<SignerSet*>(buf);

    Env::Halt_if(pay.m_SignerIdx >= ss.m_nSigners);
    Env::Halt_if(ss.m_nSigners <= 1); // cannot remove last signer

    PubKey* signers = ss.Signers();
    for (uint8_t i = pay.m_SignerIdx; i + 1 < ss.m_nSigners; i++)
        _POD_(signers[i]) = signers[i + 1];

    ss.m_nSigners--;
    ss.m_Threshold = pay.m_NewThreshold;
    Env::Halt_if(!ss.IsValid());
    SaveFull(sk, buf, ss.ValSize());
}

static void ExecCreateList(const PubKey& pkAccount,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadCreateList));
    const auto& pay = *reinterpret_cast<const PayloadCreateList*>(&p + 1);
    Env::Halt_if(pay.m_ListType != ListType::s_Single &&
                 pay.m_ListType != ListType::s_Multi);

    List::Key lk;
    _POD_(lk.m_pkAccount) = pkAccount;
    _POD_(lk.m_pkList)    = pay.m_pkList;

    List chk;
    Env::Halt_if(Env::LoadVar_T(lk, chk)); // must not already exist

    uint8_t  listBuf[s_ListValMax];
    List&    hdr = *reinterpret_cast<List*>(listBuf);
    hdr.m_hCreated   = Env::get_Height();
    hdr.m_nAssets    = 0;
    hdr.m_nProposals = 0;
    hdr.m_ListType   = pay.m_ListType;

    uint8_t  strBuf[s_ListStrBufMax];
    uint32_t nStr = PackListStrings(strBuf, pay.m_nNameLen, pay.m_nDescLen, &pay + 1);
    Env::Memcpy(listBuf + sizeof(List), strBuf, nStr);
    SaveFull(lk, listBuf, sizeof(List) + nStr);

    MyState s;
    s.m_nLists++;
    s.Save();
    SendFee(s.m_Settings.m_cidDaoVault, s.m_Settings.m_FeeList);
}

// ============================================================
// Account action executors — balance withdrawal & list transfer
// ============================================================

static void ExecWithdrawBalance(const PubKey& pkAccount,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadWithdrawBalance));
    const auto& pay = *reinterpret_cast<const PayloadWithdrawBalance*>(&p + 1);
    Env::Halt_if(!pay.m_Amount);

    AccountBalance::Key bk;
    _POD_(bk.m_pkAccount) = pkAccount;
    AccountBalance bal;
    Env::Halt_if(!Env::LoadVar_T(bk, bal));
    Env::Halt_if(pay.m_Amount > bal.m_Amount);

    // Create (or add to) a PendingClaim for the designated recipient
    PendingClaim::Key ck;
    _POD_(ck.m_pkRecipient) = pay.m_pkRecipient;
    _POD_(ck.m_pkAccount)   = pkAccount;
    PendingClaim claim;
    if (Env::LoadVar_T(ck, claim))
        Strict::Add(claim.m_Amount, pay.m_Amount);
    else
        claim.m_Amount = pay.m_Amount;
    Env::SaveVar_T(ck, claim);

    // Deduct from account balance; delete the record when empty
    bal.m_Amount -= pay.m_Amount;
    if (!bal.m_Amount)
        Env::DelVar_T(bk);
    else
        Env::SaveVar_T(bk, bal);
}

static void ExecInitiateTransfer(const PubKey& pkAccount,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadInitiateTransfer));
    const auto& pay = *reinterpret_cast<const PayloadInitiateTransfer*>(&p + 1);

    // List must exist and belong to this account
    List::Key lk;
    _POD_(lk.m_pkAccount) = pkAccount;
    _POD_(lk.m_pkList)    = pay.m_pkList;
    List lst;
    Env::Halt_if(!Env::LoadVar_T(lk, lst));

    // Destination account must exist
    Account::Key ak;
    _POD_(ak.m_pkAccount) = pay.m_pkAccountDest;
    Account acct;
    Env::Halt_if(!Env::LoadVar_T(ak, acct));

    // No duplicate pending transfer for this list
    PendingTransfer::Key ptk;
    _POD_(ptk.m_pkAccountSrc) = pkAccount;
    _POD_(ptk.m_pkList)       = pay.m_pkList;
    PendingTransfer pt;
    Env::Halt_if(Env::LoadVar_T(ptk, pt)); // must not already exist

    _POD_(pt.m_pkAccountDest) = pay.m_pkAccountDest;
    pt.m_hExpire = Env::get_Height() + MyState().m_Settings.m_ProposalTtl;
    Env::SaveVar_T(ptk, pt);
}

static void ExecCancelTransfer(const PubKey& pkAccount,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadCancelTransfer));
    const auto& pay = *reinterpret_cast<const PayloadCancelTransfer*>(&p + 1);

    PendingTransfer::Key ptk;
    _POD_(ptk.m_pkAccountSrc) = pkAccount;
    _POD_(ptk.m_pkList)       = pay.m_pkList;
    Env::Halt_if(!Env::DelVar_T(ptk));
}

static void ExecAcceptTransfer(const PubKey& pkAccountDest,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadAcceptTransfer));
    const auto& pay = *reinterpret_cast<const PayloadAcceptTransfer*>(&p + 1);

    // Load and validate pending transfer
    PendingTransfer::Key ptk;
    _POD_(ptk.m_pkAccountSrc) = pay.m_pkAccountSrc;
    _POD_(ptk.m_pkList)       = pay.m_pkList;
    PendingTransfer pt;
    Env::Halt_if(!Env::LoadVar_T(ptk, pt));
    Env::Halt_if(!PkEq(pt.m_pkAccountDest, pkAccountDest));
    Env::Halt_if(Env::get_Height() > pt.m_hExpire);

    const PubKey& pkSrc  = pay.m_pkAccountSrc;
    const PubKey& pkDest = pkAccountDest;
    const PubKey& pkList = pay.m_pkList;

    // Destination must not already own a list with the same pkList
    List::Key dstLk;
    _POD_(dstLk.m_pkAccount) = pkDest;
    _POD_(dstLk.m_pkList)    = pkList;
    List chk;
    Env::Halt_if(Env::LoadVar_T(dstLk, chk));

    // — migrate list header + strings —
    List::Key srcLk;
    _POD_(srcLk.m_pkAccount) = pkSrc;
    _POD_(srcLk.m_pkList)    = pkList;
    uint8_t lstBuf[s_ListValMax];
    uint32_t nListBytes = LoadFull(srcLk, lstBuf, sizeof(lstBuf), sizeof(List));
    List& lst = *reinterpret_cast<List*>(lstBuf);
    lst.m_nProposals = 0; // pending proposals are stale; reset the counter
    SaveFull(dstLk, lstBuf, nListBytes);
    Env::DelVar_T(srcLk);

    // — discard all pending list proposals (stale after ownership change) —
    {
        ListProposal::Key p0, p1;
        p0.m_Tag = Tags::s_ListProposal; _POD_(p0.m_pkAccount) = pkSrc; _POD_(p0.m_pkList) = pkList; p0.m_ID = 0;
        p1.m_Tag = Tags::s_ListProposal; _POD_(p1.m_pkAccount) = pkSrc; _POD_(p1.m_pkList) = pkList; p1.m_ID = static_cast<uint32_t>(-1);
        for (Env::VarReader vr(p0, p1); ; )
        {
            ListProposal::Key ekey; Proposal prop;
            if (!vr.MoveNext_T(ekey, prop)) break;
            Env::DelVar_T(ekey);
        }
    }

    // — migrate list-specific signers if present —
    {
        ListSigners::Key srcLsk;
        srcLsk.m_Tag = Tags::s_ListSigners; _POD_(srcLsk.m_pkAccount) = pkSrc; _POD_(srcLsk.m_pkList) = pkList;
        ListSigners::Key dstLsk;
        dstLsk.m_Tag = Tags::s_ListSigners; _POD_(dstLsk.m_pkAccount) = pkDest; _POD_(dstLsk.m_pkList) = pkList;
        uint8_t ssBuf[s_SignerSetMaxValSize];
        uint32_t n = Env::LoadVar(&srcLsk, sizeof(srcLsk), ssBuf, sizeof(ssBuf), KeyTag::Internal);
        if (n >= sizeof(SignerSet))
        {
            SaveFull(dstLsk, ssBuf, n);
            Env::DelVar_T(srcLsk);
        }
    }

    // — migrate asset entries —
    if (lst.m_ListType == ListType::s_Single)
    {
        ListAsset::Key k0, k1;
        k0.m_Tag = Tags::s_ListAsset; _POD_(k0.m_pkAccount) = pkSrc; _POD_(k0.m_pkList) = pkList; k0.m_Aid = 0;
        k1.m_Tag = Tags::s_ListAsset; _POD_(k1.m_pkAccount) = pkSrc; _POD_(k1.m_pkList) = pkList; k1.m_Aid = static_cast<AssetID>(-1);
        for (Env::VarReader vr(k0, k1); ; )
        {
            ListAsset::Key srcKey; ListAsset la;
            if (!vr.MoveNext_T(srcKey, la)) break;
            ListAsset::Key dstKey = srcKey;
            _POD_(dstKey.m_pkAccount) = pkDest;
            Env::SaveVar_T(dstKey, la);
            Env::DelVar_T(srcKey);
        }
    }
    else
    {
        AssetGroup::Key k0, k1;
        k0.m_Tag = Tags::s_ListAssetGroup; _POD_(k0.m_pkAccount) = pkSrc; _POD_(k0.m_pkList) = pkList; k0.m_RealAid = 0;
        k1.m_Tag = Tags::s_ListAssetGroup; _POD_(k1.m_pkAccount) = pkSrc; _POD_(k1.m_pkList) = pkList; k1.m_RealAid = static_cast<AssetID>(-1);
        uint8_t agBuf[s_AssetGroupMaxValSize];
        for (Env::VarReader vr(k0, k1); ; )
        {
            AssetGroup::Key srcKey; AssetGroup agHdr;
            if (!vr.MoveNext_T(srcKey, agHdr)) break;
            uint32_t nBytes = LoadFull(srcKey, agBuf, sizeof(agBuf), sizeof(AssetGroup));
            AssetGroup::Key dstKey = srcKey;
            _POD_(dstKey.m_pkAccount) = pkDest;
            SaveFull(dstKey, agBuf, nBytes);
            Env::DelVar_T(srcKey);
        }
    }

    // — remove the pending transfer record —
    Env::DelVar_T(ptk);
    // m_nLists is unchanged globally (the list moved, not created or destroyed)
}

static void DispatchAccountAction(const PubKey& pkAccount,
    const Proposal& p, uint32_t nPropBytes)
{
    uint32_t nPay = nPropBytes > sizeof(Proposal) ? nPropBytes - sizeof(Proposal) : 0;
    switch (p.m_Action)
    {
    case AccountActions::s_UpdateInfo:       ExecUpdateAccountInfo(pkAccount, p, nPay); break;
    case AccountActions::s_DeleteAccount:    ExecDeleteAccount(pkAccount);              break;
    case AccountActions::s_AddSigner:        ExecAddSigner(pkAccount, p, nPay);         break;
    case AccountActions::s_RemoveSigner:     ExecRemoveSigner(pkAccount, p, nPay);      break;
    case AccountActions::s_CreateList:       ExecCreateList(pkAccount, p, nPay);        break;
    case AccountActions::s_WithdrawBalance:  ExecWithdrawBalance(pkAccount, p, nPay);   break;
    case AccountActions::s_InitiateTransfer: ExecInitiateTransfer(pkAccount, p, nPay);  break;
    case AccountActions::s_CancelTransfer:   ExecCancelTransfer(pkAccount, p, nPay);    break;
    case AccountActions::s_AcceptTransfer:   ExecAcceptTransfer(pkAccount, p, nPay);    break;
    default: Env::Halt();
    }
}

// ============================================================
// List action executors
// ============================================================

static void ExecUpdateListInfo(const PubKey& pkAccount, const PubKey& pkList,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadUpdateListInfo));
    const auto& pay = *reinterpret_cast<const PayloadUpdateListInfo*>(&p + 1);

    List::Key lk;
    _POD_(lk.m_pkAccount) = pkAccount;
    _POD_(lk.m_pkList)    = pkList;

    uint8_t  buf[s_ListValMax];
    uint32_t nOld = LoadFull(lk, buf, sizeof(buf), sizeof(List));

    uint8_t  strBuf[s_ListStrBufMax];
    uint32_t nStr = PackListStrings(strBuf, pay.m_nNameLen, pay.m_nDescLen, &pay + 1);
    Env::Memcpy(buf + sizeof(List), strBuf, nStr);
    SaveFull(lk, buf, sizeof(List) + nStr);
    (void) nOld;
}

// Delete all ListAsset entries for a single-asset list
static void DeleteSingleAssetEntries(const PubKey& pkAccount, const PubKey& pkList)
{
    ListAsset::Key k0, k1;
    k0.m_Tag = Tags::s_ListAsset; _POD_(k0.m_pkAccount) = pkAccount; _POD_(k0.m_pkList) = pkList; k0.m_Aid = 0;
    k1.m_Tag = Tags::s_ListAsset; _POD_(k1.m_pkAccount) = pkAccount; _POD_(k1.m_pkList) = pkList; k1.m_Aid = static_cast<AssetID>(-1);

    for (Env::VarReader vr(k0, k1); ; )
    {
        ListAsset::Key ekey; ListAsset la;
        if (!vr.MoveNext_T(ekey, la)) break;
        Env::DelVar_T(ekey);
    }
}

// Delete all AssetGroup entries for a multi-asset list
static void DeleteMultiAssetEntries(const PubKey& pkAccount, const PubKey& pkList)
{
    AssetGroup::Key k0, k1;
    k0.m_Tag = Tags::s_ListAssetGroup; _POD_(k0.m_pkAccount) = pkAccount; _POD_(k0.m_pkList) = pkList; k0.m_RealAid = 0;
    k1.m_Tag = Tags::s_ListAssetGroup; _POD_(k1.m_pkAccount) = pkAccount; _POD_(k1.m_pkList) = pkList; k1.m_RealAid = static_cast<AssetID>(-1);

    for (Env::VarReader vr(k0, k1); ; )
    {
        AssetGroup::Key ekey; AssetGroup ag;
        if (!vr.MoveNext_T(ekey, ag)) break;
        Env::DelVar_T(ekey);
    }
}

static void ExecDeleteList(const PubKey& pkAccount, const PubKey& pkList)
{
    List::Key lk;
    _POD_(lk.m_pkAccount) = pkAccount;
    _POD_(lk.m_pkList)    = pkList;

    List lst;
    Env::Halt_if(!Env::LoadVar_T(lk, lst));

    if (lst.m_nAssets > 0)
    {
        if (lst.m_ListType == ListType::s_Single)
            DeleteSingleAssetEntries(pkAccount, pkList);
        else
            DeleteMultiAssetEntries(pkAccount, pkList);
    }

    // Remove list-specific signers if any
    ListSigners::Key lsk;
    _POD_(lsk.m_pkAccount) = pkAccount;
    _POD_(lsk.m_pkList)    = pkList;
    Env::DelVar_T(lsk);

    Env::DelVar_T(lk);

    MyState s;
    s.m_nLists--;
    s.Save();
}

// Single-asset: add or remove one AssetID entry
static void ExecSingleAssetOp(const PubKey& pkAccount, const PubKey& pkList,
    const Proposal& p, uint32_t nPayload, bool bAdd)
{
    Env::Halt_if(nPayload < sizeof(PayloadAsset));
    const auto& pay = *reinterpret_cast<const PayloadAsset*>(&p + 1);

    List::Key lk;
    _POD_(lk.m_pkAccount) = pkAccount;
    _POD_(lk.m_pkList)    = pkList;

    uint8_t  listBuf[s_ListValMax];
    uint32_t nListBytes = LoadFull(lk, listBuf, sizeof(listBuf), sizeof(List));
    List&    lst = *reinterpret_cast<List*>(listBuf);
    Env::Halt_if(lst.m_ListType != ListType::s_Single);

    ListAsset::Key lak;
    lak.m_Tag = Tags::s_ListAsset;
    _POD_(lak.m_pkAccount) = pkAccount;
    _POD_(lak.m_pkList)    = pkList;
    lak.m_Aid = pay.m_Aid;

    if (bAdd)
    {
        ListAsset existing;
        Env::Halt_if(Env::LoadVar_T(lak, existing));
        ListAsset la; la.m_Dummy = 0;
        Env::SaveVar_T(lak, la);
        lst.m_nAssets++;
    }
    else
    {
        Env::Halt_if(!Env::DelVar_T(lak));
        lst.m_nAssets--;
    }
    SaveFull(lk, listBuf, nListBytes);
}

// Multi-asset: add or replace a group entry (upsert by real AssetID)
static void ExecAddAssetGroup(const PubKey& pkAccount, const PubKey& pkList,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadAddAssetGroup));
    const auto& pay = *reinterpret_cast<const PayloadAddAssetGroup*>(&p + 1);
    Env::Halt_if(pay.m_nFakes > s_MaxFakes);
    Env::Halt_if(nPayload < sizeof(PayloadAddAssetGroup) + pay.m_nFakes * sizeof(AssetID));

    List::Key lk;
    _POD_(lk.m_pkAccount) = pkAccount;
    _POD_(lk.m_pkList)    = pkList;

    uint8_t  listBuf[s_ListValMax];
    uint32_t nListBytes = LoadFull(lk, listBuf, sizeof(listBuf), sizeof(List));
    List&    lst = *reinterpret_cast<List*>(listBuf);
    Env::Halt_if(lst.m_ListType != ListType::s_Multi);

    AssetGroup::Key gk;
    gk.m_Tag = Tags::s_ListAssetGroup;
    _POD_(gk.m_pkAccount) = pkAccount;
    _POD_(gk.m_pkList)    = pkList;
    gk.m_RealAid = Utils::FromBE(pay.m_RealAid); // big-endian in key

    // Check whether this group already exists (for counter tracking)
    bool bExisting = false;
    {
        AssetGroup existing;
        bExisting = Env::LoadVar_T(gk, existing);
    }

    // Build and save the new group value
    uint8_t  agBuf[s_AssetGroupMaxValSize];
    AssetGroup& ag = *reinterpret_cast<AssetGroup*>(agBuf);
    ag.m_nFakes = pay.m_nFakes;
    Env::Memcpy(agBuf + sizeof(AssetGroup),
        reinterpret_cast<const uint8_t*>(&pay + 1),
        pay.m_nFakes * sizeof(AssetID));
    SaveFull(gk, agBuf, ag.ValSize());

    if (!bExisting)
    {
        lst.m_nAssets++;
        SaveFull(lk, listBuf, nListBytes);
    }
}

// Multi-asset: remove a group by real AssetID
static void ExecRemoveAssetGroup(const PubKey& pkAccount, const PubKey& pkList,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(PayloadRemoveAssetGroup));
    const auto& pay = *reinterpret_cast<const PayloadRemoveAssetGroup*>(&p + 1);

    List::Key lk;
    _POD_(lk.m_pkAccount) = pkAccount;
    _POD_(lk.m_pkList)    = pkList;

    uint8_t  listBuf[s_ListValMax];
    uint32_t nListBytes = LoadFull(lk, listBuf, sizeof(listBuf), sizeof(List));
    List&    lst = *reinterpret_cast<List*>(listBuf);
    Env::Halt_if(lst.m_ListType != ListType::s_Multi);

    AssetGroup::Key gk;
    gk.m_Tag = Tags::s_ListAssetGroup;
    _POD_(gk.m_pkAccount) = pkAccount;
    _POD_(gk.m_pkList)    = pkList;
    gk.m_RealAid = Utils::FromBE(pay.m_RealAid);

    Env::Halt_if(!Env::DelVar_T(gk));

    lst.m_nAssets--;
    SaveFull(lk, listBuf, nListBytes);
}

static void ExecSetListManagers(const PubKey& pkAccount, const PubKey& pkList,
    const Proposal& p, uint32_t nPayload)
{
    Env::Halt_if(nPayload < sizeof(SignerSet));
    const SignerSet& ss = *reinterpret_cast<const SignerSet*>(&p + 1);
    Env::Halt_if(!ss.IsValid());
    Env::Halt_if(nPayload < ss.ValSize());

    ListSigners::Key lsk;
    _POD_(lsk.m_pkAccount) = pkAccount;
    _POD_(lsk.m_pkList)    = pkList;
    SaveFull(lsk, &ss, ss.ValSize());
}

static void ExecClearListManagers(const PubKey& pkAccount, const PubKey& pkList)
{
    ListSigners::Key lsk;
    _POD_(lsk.m_pkAccount) = pkAccount;
    _POD_(lsk.m_pkList)    = pkList;
    Env::DelVar_T(lsk); // no-op if not present
}

static void DispatchListAction(const PubKey& pkAccount, const PubKey& pkList,
    const Proposal& p, uint32_t nPropBytes)
{
    uint32_t nPay = nPropBytes > sizeof(Proposal) ? nPropBytes - sizeof(Proposal) : 0;
    switch (p.m_Action)
    {
    case ListActions::s_UpdateInfo:
        ExecUpdateListInfo(pkAccount, pkList, p, nPay); break;
    case ListActions::s_DeleteList:
        ExecDeleteList(pkAccount, pkList); break;
    case ListActions::s_AddAsset:
        ExecSingleAssetOp(pkAccount, pkList, p, nPay, true); break;
    case ListActions::s_RemoveAsset:
        ExecSingleAssetOp(pkAccount, pkList, p, nPay, false); break;
    case ListActions::s_AddAssetGroup:
        ExecAddAssetGroup(pkAccount, pkList, p, nPay); break;
    case ListActions::s_RemoveAssetGroup:
        ExecRemoveAssetGroup(pkAccount, pkList, p, nPay); break;
    case ListActions::s_SetListManagers:
        ExecSetListManagers(pkAccount, pkList, p, nPay); break;
    case ListActions::s_ClearListManagers:
        ExecClearListManagers(pkAccount, pkList); break;
    default: Env::Halt();
    }
}

// ============================================================
// Proposal creation helper (shared between ProposeAccount/List)
// ============================================================

// Builds a Proposal into propBuf, copying nPayload bytes from pPayload.
// Tries to auto-cast a yes vote from the proposer if they are in the signer set.
// Returns the VoteResult so the caller can decide whether to execute immediately.
static VoteResult BuildProposal(
    uint8_t* propBuf, uint32_t& nPropBytes,
    const PubKey& pkProposer, uint8_t action,
    const void* pPayload, uint32_t nPayload,
    const SignerSet& ss)
{
    Proposal& prop = *reinterpret_cast<Proposal*>(propBuf);
    _POD_(prop.m_pkProposer) = pkProposer;
    prop.m_hExpire  = Env::get_Height() + MyState().m_Settings.m_ProposalTtl;
    prop.m_Action   = action;
    prop.m_YesMask  = 0;
    prop.m_NoMask   = 0;
    Env::Memcpy(propBuf + sizeof(Proposal), pPayload, nPayload);
    nPropBytes = sizeof(Proposal) + nPayload;

    return AutoVoteIfSigner(prop, ss, pkProposer);
}

// ============================================================
// Ctor / Dtor
// ============================================================

BEAM_EXPORT void Ctor(const Method::Init& r)
{
    r.m_Upgradable.TestNumApprovers();
    r.m_Upgradable.Save();
    Env::Halt_if(!Env::RefAdd(r.m_Settings.m_cidDaoVault));

    State s;
    _POD_(s).SetZero();
    _POD_(s.m_Settings) = r.m_Settings;
    Env::SaveVar_T((uint8_t) State::s_Key, s);
}

BEAM_EXPORT void Dtor(void*)
{
    State s;
    Env::LoadVar_T((uint8_t) State::s_Key, s);
    Env::RefRelease(s.m_Settings.m_cidDaoVault);
    Env::DelVar_T((uint8_t) State::s_Key);
}

BEAM_EXPORT void Method_2(void*) {} // upgrade hook

// ============================================================
// Method_3 — CreateAccount
// ============================================================

BEAM_EXPORT void Method_3(const Method::CreateAccount& r)
{
    Env::Halt_if(r.m_nSigners  < 1 || r.m_nSigners  > s_MaxSigners);
    Env::Halt_if(r.m_Threshold < 1 || r.m_Threshold > r.m_nSigners);

    // Variable tail: signers[], name[], website[], desc[]
    const uint8_t* pTail = reinterpret_cast<const uint8_t*>(&r + 1);

    // Build SignerSet
    uint8_t ssBuf[s_SignerSetMaxValSize];
    SignerSet& ss = *reinterpret_cast<SignerSet*>(ssBuf);
    ss.m_Threshold = r.m_Threshold;
    ss.m_nSigners  = r.m_nSigners;
    uint32_t nSignersBytes = r.m_nSigners * sizeof(PubKey);
    Env::Memcpy(ss.Signers(), pTail, nSignersBytes);
    pTail += nSignersBytes;

    // Pack profile strings
    uint8_t  strBuf[s_AccountStrBufMax];
    uint32_t nStr = PackAccountStrings(strBuf, r.m_nNameLen, r.m_nWebsiteLen, r.m_nDescLen, pTail);

    // Ensure account does not exist
    Account::Key ak;
    _POD_(ak.m_pkAccount) = r.m_pkAccount;
    Account existing;
    Env::Halt_if(Env::LoadVar_T(ak, existing));

    // Save account header + strings
    uint8_t  accountBuf[s_AccountValMax];
    Account& acct = *reinterpret_cast<Account*>(accountBuf);
    acct.m_hCreated   = Env::get_Height();
    acct.m_nProposals = 0;
    Env::Memcpy(accountBuf + sizeof(Account), strBuf, nStr);
    SaveFull(ak, accountBuf, sizeof(Account) + nStr);

    // Save signer set
    AccountSigners::Key sk;
    _POD_(sk.m_pkAccount) = r.m_pkAccount;
    SaveFull(sk, ssBuf, ss.ValSize());

    MyState s;
    s.m_nAccounts++;
    s.Save();
    SendFee(s.m_Settings.m_cidDaoVault, s.m_Settings.m_FeeAccount);

    Env::AddSig(r.m_pkAccount);
}

// ============================================================
// Method_4 — ProposeAccountAction
// ============================================================

BEAM_EXPORT void Method_4(const Method::ProposeAccountAction& r)
{
    // Account must exist; load it (we need m_nProposals)
    Account::Key ak;
    _POD_(ak.m_pkAccount) = r.m_pkAccount;
    uint8_t  acctBuf[s_AccountValMax];
    uint32_t nAcct = LoadFull(ak, acctBuf, sizeof(acctBuf), sizeof(Account));
    Account& acct = *reinterpret_cast<Account*>(acctBuf);

    // Load signer set
    uint8_t ssBuf[s_SignerSetMaxValSize];
    LoadAccountSigners(r.m_pkAccount, ssBuf);
    const SignerSet& ss = *reinterpret_cast<const SignerSet*>(ssBuf);

    // Determine payload extent
    const uint8_t* pPayload = reinterpret_cast<const uint8_t*>(&r + 1);
    uint32_t nPayload = 0;

    switch (r.m_Action)
    {
    case AccountActions::s_UpdateInfo:
    {
        const auto& pay = *reinterpret_cast<const PayloadUpdateAccountInfo*>(pPayload);
        nPayload = sizeof(PayloadUpdateAccountInfo)
            + pay.m_nNameLen + pay.m_nWebsiteLen + (uint32_t)pay.m_nDescLen;
        break;
    }
    case AccountActions::s_DeleteAccount:
        nPayload = 0;
        break;
    case AccountActions::s_AddSigner:
        nPayload = sizeof(PayloadAddSigner);
        break;
    case AccountActions::s_RemoveSigner:
        nPayload = sizeof(PayloadRemoveSigner);
        break;
    case AccountActions::s_CreateList:
    {
        const auto& pay = *reinterpret_cast<const PayloadCreateList*>(pPayload);
        nPayload = sizeof(PayloadCreateList)
            + pay.m_nNameLen + (uint32_t)pay.m_nDescLen;
        Env::AddSig(pay.m_pkList); // prove control of new list identity key
        break;
    }
    case AccountActions::s_WithdrawBalance:
        nPayload = sizeof(PayloadWithdrawBalance);
        break;
    case AccountActions::s_InitiateTransfer:
        nPayload = sizeof(PayloadInitiateTransfer);
        break;
    case AccountActions::s_CancelTransfer:
        nPayload = sizeof(PayloadCancelTransfer);
        break;
    case AccountActions::s_AcceptTransfer:
        nPayload = sizeof(PayloadAcceptTransfer);
        break;
    default: Env::Halt();
    }

    // Non-signer proposal fee: half to DAO Vault, half to account balance
    {
        MyState s;
        bool bIsSigner = FindSignerIdx(ss, r.m_pkProposer) < ss.m_nSigners;
        if (!bIsSigner)
        {
            Amount fee = s.m_Settings.m_FeeProposal;
            if (fee)
            {
                Env::FundsLock(0, fee);
                Amount halfDao = fee / 2;
                SendFee(s.m_Settings.m_cidDaoVault, halfDao);
                AddAccountBalance(r.m_pkAccount, fee - halfDao);
            }
        }
    }

    // Build proposal; try auto-vote if proposer is a signer
    AccountProposal::Key pk;
    pk.m_Tag = Tags::s_AccountProposal;
    _POD_(pk.m_pkAccount) = r.m_pkAccount;
    pk.m_ID = Utils::FromBE(acct.m_nProposals);

    uint8_t  propBuf[s_ProposalValMax];
    uint32_t nProp = 0;
    VoteResult vr = BuildProposal(propBuf, nProp,
        r.m_pkProposer, r.m_Action, pPayload, nPayload, ss);

    if (vr.execute)
    {
        Proposal& prop = *reinterpret_cast<Proposal*>(propBuf);
        DispatchAccountAction(r.m_pkAccount, prop, nProp);
        // proposal not stored; no ID increment needed? We still increment
        // so IDs remain unique across cancelled/fast-path proposals.
    }
    else
    {
        SaveFull(pk, propBuf, nProp);
    }

    acct.m_nProposals++;
    SaveFull(ak, acctBuf, nAcct);

    Env::AddSig(r.m_pkProposer);
}

// ============================================================
// Method_5 — VoteAccountProposal
// ============================================================

BEAM_EXPORT void Method_5(const Method::VoteAccountProposal& r)
{
    uint8_t ssBuf[s_SignerSetMaxValSize];
    LoadAccountSigners(r.m_pkAccount, ssBuf);
    const SignerSet& ss = *reinterpret_cast<const SignerSet*>(ssBuf);
    Env::Halt_if(r.m_SignerIdx >= ss.m_nSigners);

    AccountProposal::Key pk;
    pk.m_Tag = Tags::s_AccountProposal;
    _POD_(pk.m_pkAccount) = r.m_pkAccount;
    pk.m_ID = Utils::FromBE(r.m_ProposalID);

    uint8_t  propBuf[s_ProposalValMax];
    uint32_t nProp = LoadFull(pk, propBuf, sizeof(propBuf), sizeof(Proposal));
    Proposal& prop = *reinterpret_cast<Proposal*>(propBuf);

    VoteResult vr = ApplyVote(prop, ss, r.m_SignerIdx, r.m_bYes);

    if (vr.execute)
    {
        DispatchAccountAction(r.m_pkAccount, prop, nProp);
        Env::DelVar_T(pk);
    }
    else if (vr.reject)
    {
        Env::DelVar_T(pk);
    }
    else
    {
        SaveFull(pk, propBuf, nProp);
    }

    Env::AddSig(ss.Signers()[r.m_SignerIdx]);
}

// ============================================================
// Method_6 — CancelAccountProposal
// ============================================================

BEAM_EXPORT void Method_6(const Method::CancelAccountProposal& r)
{
    AccountProposal::Key pk;
    pk.m_Tag = Tags::s_AccountProposal;
    _POD_(pk.m_pkAccount) = r.m_pkAccount;
    pk.m_ID = Utils::FromBE(r.m_ProposalID);

    Proposal prop;
    Env::Halt_if(!Env::LoadVar_T(pk, prop));
    Env::Halt_if(!PkEq(prop.m_pkProposer, r.m_pkProposer));
    Env::DelVar_T(pk);

    Env::AddSig(r.m_pkProposer);
}

// ============================================================
// Method_7 — ProposeListAction
// ============================================================

BEAM_EXPORT void Method_7(const Method::ProposeListAction& r)
{
    List::Key lk;
    _POD_(lk.m_pkAccount) = r.m_pkAccount;
    _POD_(lk.m_pkList)    = r.m_pkList;
    uint8_t  listBuf[s_ListValMax];
    uint32_t nList = LoadFull(lk, listBuf, sizeof(listBuf), sizeof(List));
    List&    lst   = *reinterpret_cast<List*>(listBuf);

    uint8_t ssBuf[s_SignerSetMaxValSize];
    LoadListEffectiveSigners(r.m_pkAccount, r.m_pkList, ssBuf);
    const SignerSet& ss = *reinterpret_cast<const SignerSet*>(ssBuf);

    const uint8_t* pPayload = reinterpret_cast<const uint8_t*>(&r + 1);
    uint32_t nPayload = 0;

    switch (r.m_Action)
    {
    case ListActions::s_UpdateInfo:
    {
        const auto& pay = *reinterpret_cast<const PayloadUpdateListInfo*>(pPayload);
        nPayload = sizeof(PayloadUpdateListInfo)
            + pay.m_nNameLen + (uint32_t)pay.m_nDescLen;
        break;
    }
    case ListActions::s_DeleteList:
        nPayload = 0;
        break;
    case ListActions::s_AddAsset:
    case ListActions::s_RemoveAsset:
        Env::Halt_if(lst.m_ListType != ListType::s_Single);
        nPayload = sizeof(PayloadAsset);
        break;
    case ListActions::s_AddAssetGroup:
    {
        Env::Halt_if(lst.m_ListType != ListType::s_Multi);
        const auto& pay = *reinterpret_cast<const PayloadAddAssetGroup*>(pPayload);
        Env::Halt_if(pay.m_nFakes > s_MaxFakes);
        nPayload = sizeof(PayloadAddAssetGroup) + pay.m_nFakes * sizeof(AssetID);
        break;
    }
    case ListActions::s_RemoveAssetGroup:
        Env::Halt_if(lst.m_ListType != ListType::s_Multi);
        nPayload = sizeof(PayloadRemoveAssetGroup);
        break;
    case ListActions::s_SetListManagers:
    {
        const SignerSet& newSS = *reinterpret_cast<const SignerSet*>(pPayload);
        Env::Halt_if(!newSS.IsValid());
        nPayload = newSS.ValSize();
        break;
    }
    case ListActions::s_ClearListManagers:
        nPayload = 0;
        break;
    default: Env::Halt();
    }

    // Non-signer proposal fee: half to DAO Vault, half to account balance
    {
        MyState s;
        bool bIsSigner = FindSignerIdx(ss, r.m_pkProposer) < ss.m_nSigners;
        if (!bIsSigner)
        {
            Amount fee = s.m_Settings.m_FeeProposal;
            if (fee)
            {
                Env::FundsLock(0, fee);
                Amount halfDao = fee / 2;
                SendFee(s.m_Settings.m_cidDaoVault, halfDao);
                AddAccountBalance(r.m_pkAccount, fee - halfDao);
            }
        }
    }

    ListProposal::Key lpk;
    lpk.m_Tag = Tags::s_ListProposal;
    _POD_(lpk.m_pkAccount) = r.m_pkAccount;
    _POD_(lpk.m_pkList)    = r.m_pkList;
    lpk.m_ID = Utils::FromBE(lst.m_nProposals);

    uint8_t  propBuf[s_ProposalValMax];
    uint32_t nProp = 0;
    VoteResult vr = BuildProposal(propBuf, nProp,
        r.m_pkProposer, r.m_Action, pPayload, nPayload, ss);

    if (vr.execute)
    {
        Proposal& prop = *reinterpret_cast<Proposal*>(propBuf);
        DispatchListAction(r.m_pkAccount, r.m_pkList, prop, nProp);
    }
    else
    {
        SaveFull(lpk, propBuf, nProp);
    }

    lst.m_nProposals++;
    SaveFull(lk, listBuf, nList);

    Env::AddSig(r.m_pkProposer);
}

// ============================================================
// Method_8 — VoteListProposal
// ============================================================

BEAM_EXPORT void Method_8(const Method::VoteListProposal& r)
{
    uint8_t ssBuf[s_SignerSetMaxValSize];
    LoadListEffectiveSigners(r.m_pkAccount, r.m_pkList, ssBuf);
    const SignerSet& ss = *reinterpret_cast<const SignerSet*>(ssBuf);
    Env::Halt_if(r.m_SignerIdx >= ss.m_nSigners);

    ListProposal::Key lpk;
    lpk.m_Tag = Tags::s_ListProposal;
    _POD_(lpk.m_pkAccount) = r.m_pkAccount;
    _POD_(lpk.m_pkList)    = r.m_pkList;
    lpk.m_ID = Utils::FromBE(r.m_ProposalID);

    uint8_t  propBuf[s_ProposalValMax];
    uint32_t nProp = LoadFull(lpk, propBuf, sizeof(propBuf), sizeof(Proposal));
    Proposal& prop = *reinterpret_cast<Proposal*>(propBuf);

    VoteResult vr = ApplyVote(prop, ss, r.m_SignerIdx, r.m_bYes);

    if (vr.execute)
    {
        DispatchListAction(r.m_pkAccount, r.m_pkList, prop, nProp);
        Env::DelVar_T(lpk);
    }
    else if (vr.reject)
    {
        Env::DelVar_T(lpk);
    }
    else
    {
        SaveFull(lpk, propBuf, nProp);
    }

    Env::AddSig(ss.Signers()[r.m_SignerIdx]);
}

// ============================================================
// Method_9 — CancelListProposal
// ============================================================

BEAM_EXPORT void Method_9(const Method::CancelListProposal& r)
{
    ListProposal::Key lpk;
    lpk.m_Tag = Tags::s_ListProposal;
    _POD_(lpk.m_pkAccount) = r.m_pkAccount;
    _POD_(lpk.m_pkList)    = r.m_pkList;
    lpk.m_ID = Utils::FromBE(r.m_ProposalID);

    Proposal prop;
    Env::Halt_if(!Env::LoadVar_T(lpk, prop));
    Env::Halt_if(!PkEq(prop.m_pkProposer, r.m_pkProposer));
    Env::DelVar_T(lpk);

    Env::AddSig(r.m_pkProposer);
}

// ============================================================
// Method_10 — CleanupProposal  (anyone can delete expired proposals)
// ============================================================

BEAM_EXPORT void Method_10(const Method::CleanupProposal& r)
{
    Proposal prop;

    if (r.m_bAccountProposal)
    {
        AccountProposal::Key pk;
        pk.m_Tag = Tags::s_AccountProposal;
        _POD_(pk.m_pkAccount) = r.m_pkAccount;
        pk.m_ID = Utils::FromBE(r.m_ProposalID);
        Env::Halt_if(!Env::LoadVar_T(pk, prop));
        Env::Halt_if(Env::get_Height() <= prop.m_hExpire);
        Env::DelVar_T(pk);
    }
    else
    {
        ListProposal::Key lpk;
        lpk.m_Tag = Tags::s_ListProposal;
        _POD_(lpk.m_pkAccount) = r.m_pkAccount;
        _POD_(lpk.m_pkList)    = r.m_pkList;
        lpk.m_ID = Utils::FromBE(r.m_ProposalID);
        Env::Halt_if(!Env::LoadVar_T(lpk, prop));
        Env::Halt_if(Env::get_Height() <= prop.m_hExpire);
        Env::DelVar_T(lpk);
    }
}

// ============================================================
// Method_11 — ClaimBalance
// ============================================================

BEAM_EXPORT void Method_11(const Method::ClaimBalance& r)
{
    PendingClaim::Key ck;
    _POD_(ck.m_pkRecipient) = r.m_pkRecipient;
    _POD_(ck.m_pkAccount)   = r.m_pkAccount;

    PendingClaim claim;
    Env::Halt_if(!Env::LoadVar_T(ck, claim));
    Env::DelVar_T(ck);

    Env::FundsUnlock(0, claim.m_Amount);
    Env::AddSig(r.m_pkRecipient);
}

// ============================================================
// Method_12 — CleanupTransfer  (anyone can delete expired transfer records)
// ============================================================

BEAM_EXPORT void Method_12(const Method::CleanupTransfer& r)
{
    PendingTransfer::Key ptk;
    _POD_(ptk.m_pkAccountSrc) = r.m_pkAccount;
    _POD_(ptk.m_pkList)       = r.m_pkList;

    PendingTransfer pt;
    Env::Halt_if(!Env::LoadVar_T(ptk, pt));
    Env::Halt_if(Env::get_Height() <= pt.m_hExpire);
    Env::DelVar_T(ptk);
    // No signature required
}

} // namespace AssetLists

// ============================================================
// Upgradable3
// ============================================================

namespace Upgradable3 {

    const uint32_t g_CurrentVersion = _countof(AssetLists::s_pSID) - 1;
    uint32_t get_CurrentVersion() { return g_CurrentVersion; }

    void OnUpgraded(uint32_t nPrevVersion)
    {
        if constexpr (g_CurrentVersion)
            Env::Halt_if(nPrevVersion != g_CurrentVersion - 1);
        else
            Env::Halt();
    }

} // namespace Upgradable3
