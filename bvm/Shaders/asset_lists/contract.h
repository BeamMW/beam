#pragma once
#include "../upgradable3/contract.h"

namespace AssetLists
{
    static const ShaderID s_pSID[] = {
        { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
          0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, // placeholder
    };

#pragma pack(push, 1)

    // ----------------------------------------------------------------
    // Key tags
    // ----------------------------------------------------------------

    struct Tags
    {
        static const uint8_t s_Settings         = 0;
        static const uint8_t s_Account          = 1;
        static const uint8_t s_AccountSigners   = 2;
        static const uint8_t s_AccountProposal  = 3;
        static const uint8_t s_List             = 4;
        static const uint8_t s_ListSigners      = 5;
        static const uint8_t s_ListProposal     = 6;
        static const uint8_t s_ListAsset        = 7; // single-asset list entry
        static const uint8_t s_ListAssetGroup   = 8; // multi-asset list entry
        static const uint8_t s_AccountBalance   = 9;  // claimable BEAM accumulated from proposal fees
        static const uint8_t s_PendingClaim     = 10; // approved withdrawal awaiting recipient claim
        static const uint8_t s_PendingTransfer  = 11; // pending list transfer offer (src → dst)
    };

    // ----------------------------------------------------------------
    // List type discriminator
    // ----------------------------------------------------------------

    struct ListType
    {
        // Flat list of AssetIDs. Example: "approved assets", "watchlist".
        static const uint8_t s_Single = 0;

        // Mapping of real AssetID -> array of associated/fake AssetIDs.
        // Useful for imposter/bridge asset registries where one canonical
        // asset maps to multiple equivalent representations.
        static const uint8_t s_Multi  = 1;
    };

    // ----------------------------------------------------------------
    // Global settings & state
    // ----------------------------------------------------------------

    static const uint8_t s_MaxSigners  = 8;  // fits in uint8_t bitmask
    static const uint8_t s_MaxFakes    = 16; // max fake IDs per multi-asset group entry

    struct Settings
    {
        ContractID m_cidDaoVault;
        Amount     m_FeeAccount;  // groth, charged on CreateAccount (non-refundable)
        Amount     m_FeeList;     // groth, charged when CreateList proposal executes
        Amount     m_FeeProposal; // groth, charged when a non-signer creates a proposal; split 50/50 DAO/account
        Height     m_ProposalTtl; // default proposal lifetime in blocks
    };

    struct State
    {
        static const uint8_t s_Key = Tags::s_Settings;
        Settings m_Settings;
        uint32_t m_nAccounts;
        uint32_t m_nLists;
    };

    // ----------------------------------------------------------------
    // SignerSet  (variable-length value stored under *Signers keys)
    //
    // Layout in storage: SignerSet header + PubKey signers[m_nSigners]
    // ----------------------------------------------------------------

    struct SignerSet
    {
        uint8_t m_Threshold; // M
        uint8_t m_nSigners;  // N  (1 .. s_MaxSigners)

        bool IsValid() const
        {
            return m_nSigners >= 1
                && m_nSigners <= s_MaxSigners
                && m_Threshold >= 1
                && m_Threshold <= m_nSigners;
        }

        uint32_t ValSize() const
        {
            return sizeof(SignerSet) + (uint32_t)m_nSigners * sizeof(PubKey);
        }

        const PubKey* Signers() const { return reinterpret_cast<const PubKey*>(this + 1); }
              PubKey* Signers()       { return reinterpret_cast<      PubKey*>(this + 1); }

        // Minimum no-votes needed to make the yes-threshold permanently unreachable.
        uint8_t RejectThreshold() const { return m_nSigners - m_Threshold + 1; }
    };

    // Maximum serialized size of a SignerSet (header + all signers).
    // Defined here (outside struct body) to avoid sizeof on incomplete type.
    static const uint32_t s_SignerSetMaxValSize = sizeof(SignerSet) + s_MaxSigners * sizeof(PubKey);

    // ----------------------------------------------------------------
    // Account
    //
    // Key:   { s_Account, pkAccount }
    // Value: Account header + packed strings:
    //        [uint8_t nNameLen, name[], uint8_t nWebsiteLen, website[], uint16_t nDescLen, desc[]]
    // ----------------------------------------------------------------

    struct Account
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_Account;
            PubKey  m_pkAccount;
        };

        Height   m_hCreated;
        uint32_t m_nProposals; // monotonically increasing; used as proposal IDs

        static const uint32_t s_NameMaxLen    = 64;
        static const uint32_t s_WebsiteMaxLen = 128;
        static const uint32_t s_DescMaxLen    = 256;
    };

    // Key:   { s_AccountSigners, pkAccount }
    // Value: SignerSet header + PubKey[m_nSigners]
    struct AccountSigners
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_AccountSigners;
            PubKey  m_pkAccount;
        };
    };

    // ----------------------------------------------------------------
    // List
    //
    // Key:   { s_List, pkAccount, pkList }
    // Value: List header + packed strings:
    //        [uint8_t nNameLen, name[], uint16_t nDescLen, desc[]]
    //
    // m_ListType is fixed at creation and cannot be changed.
    // If no ListSigners entry exists the account's SignerSet governs
    // all list operations (fallback).
    // ----------------------------------------------------------------

    struct List
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_List;
            PubKey  m_pkAccount;
            PubKey  m_pkList;
        };

        Height   m_hCreated;
        uint32_t m_nAssets;    // item count: AssetIDs (single) or groups (multi)
        uint32_t m_nProposals; // monotonically increasing; used as proposal IDs
        uint8_t  m_ListType;   // ListType::s_Single or ListType::s_Multi

        static const uint32_t s_NameMaxLen = 64;
        static const uint32_t s_DescMaxLen = 256;
    };

    // Key:   { s_ListSigners, pkAccount, pkList }
    // Value: SignerSet header + PubKey[m_nSigners]
    // Optional: if absent the account SignerSet governs list operations.
    struct ListSigners
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_ListSigners;
            PubKey  m_pkAccount;
            PubKey  m_pkList;
        };
    };

    // ----------------------------------------------------------------
    // Single-asset list entry
    //
    // Key:   { s_ListAsset, pkAccount, pkList, AssetID }
    // Value: ListAsset (1-byte dummy) — presence = membership
    // ----------------------------------------------------------------

    struct ListAsset
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_ListAsset;
            PubKey  m_pkAccount;
            PubKey  m_pkList;
            AssetID m_Aid;
        };
        uint8_t m_Dummy;
    };

    // ----------------------------------------------------------------
    // Multi-asset list entry (one group = one real asset + N fake assets)
    //
    // Key:   { s_ListAssetGroup, pkAccount, pkList, realAssetID (big-endian) }
    // Value: AssetGroup header + AssetID fakeIds[m_nFakes]
    //
    // Adding or replacing a group is done via s_AddAssetGroup; the entire
    // fake-ID array is replaced atomically with each update.
    // ----------------------------------------------------------------

    struct AssetGroup
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_ListAssetGroup;
            PubKey  m_pkAccount;
            PubKey  m_pkList;
            AssetID m_RealAid; // stored big-endian for enumeration in sorted order
        };

        uint8_t m_nFakes; // number of fake AssetIDs that follow

        uint32_t ValSize() const
        {
            return sizeof(AssetGroup) + (uint32_t)m_nFakes * sizeof(AssetID);
        }

        const AssetID* Fakes() const { return reinterpret_cast<const AssetID*>(this + 1); }
              AssetID* Fakes()       { return reinterpret_cast<      AssetID*>(this + 1); }
    };

    // Maximum serialized size of an AssetGroup (header + all fake IDs).
    // Defined here (outside struct body) to avoid sizeof on incomplete type.
    static const uint32_t s_AssetGroupMaxValSize = sizeof(AssetGroup) + s_MaxFakes * sizeof(AssetID);

    // ----------------------------------------------------------------
    // AccountBalance  (accumulated from non-signer proposal fees)
    //
    // Key:   { s_AccountBalance, pkAccount }
    // Value: AccountBalance  (Amount)
    // ----------------------------------------------------------------

    struct AccountBalance
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_AccountBalance;
            PubKey  m_pkAccount;
        };
        Amount m_Amount;
    };

    // ----------------------------------------------------------------
    // PendingClaim  (created by s_WithdrawBalance execution)
    //
    // Key:   { s_PendingClaim, pkRecipient, pkAccount }
    // Value: PendingClaim  (Amount)
    //
    // Consumed by Method_11 ClaimBalance — the recipient calls this to
    // collect their BEAM via FundsUnlock.
    // ----------------------------------------------------------------

    struct PendingClaim
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_PendingClaim;
            PubKey  m_pkRecipient;
            PubKey  m_pkAccount; // source account whose balance this originated from
        };
        Amount m_Amount;
    };

    // ----------------------------------------------------------------
    // PendingTransfer  (created by s_InitiateTransfer execution)
    //
    // Key:   { s_PendingTransfer, pkAccountSrc, pkList }
    // Value: PendingTransfer
    //
    // Consumed by s_AcceptTransfer (migrates all list storage) or
    // s_CancelTransfer (source aborts).  Anyone may delete expired
    // records via Method_12 CleanupTransfer.
    // ----------------------------------------------------------------

    struct PendingTransfer
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_PendingTransfer;
            PubKey  m_pkAccountSrc;
            PubKey  m_pkList;
        };
        PubKey  m_pkAccountDest;
        Height  m_hExpire;
    };

    // ----------------------------------------------------------------
    // Proposal
    //
    // Each account and each list maintains its own monotonic proposal counter.
    // Stored as: Proposal header + action-specific payload bytes.
    //
    // Keys:
    //   account proposal: { s_AccountProposal, pkAccount, uint32_t ID (big-endian) }
    //   list    proposal: { s_ListProposal, pkAccount, pkList, uint32_t ID (big-endian) }
    //
    // Votes are tracked as uint8_t bitmasks (one bit per signer index).
    // ----------------------------------------------------------------

    struct Proposal
    {
        PubKey  m_pkProposer; // only the proposer can cancel
        Height  m_hExpire;
        uint8_t m_Action;    // AccountActions::* or ListActions::*
        uint8_t m_YesMask;   // bitmask of signers who voted yes
        uint8_t m_NoMask;    // bitmask of signers who voted no
        // followed by action-specific payload
    };

    struct AccountProposal
    {
        struct Key
        {
            uint8_t  m_Tag = Tags::s_AccountProposal;
            PubKey   m_pkAccount;
            uint32_t m_ID; // big-endian
        };
    };

    struct ListProposal
    {
        struct Key
        {
            uint8_t  m_Tag = Tags::s_ListProposal;
            PubKey   m_pkAccount;
            PubKey   m_pkList;
            uint32_t m_ID; // big-endian
        };
    };

    // ----------------------------------------------------------------
    // Action type constants
    // ----------------------------------------------------------------

    struct AccountActions
    {
        static const uint8_t s_UpdateInfo       = 0; // update name/website/description
        static const uint8_t s_DeleteAccount    = 1; // delete account (all lists must be cleared first)
        static const uint8_t s_AddSigner        = 2; // append signer + set new threshold
        static const uint8_t s_RemoveSigner     = 3; // remove signer by index + set new threshold
        static const uint8_t s_CreateList       = 4; // create a new list under this account
        static const uint8_t s_WithdrawBalance  = 5; // designate a recipient for accumulated fee balance
        static const uint8_t s_InitiateTransfer = 6; // propose to transfer a list to another account
        static const uint8_t s_CancelTransfer   = 7; // cancel a pending list transfer
        static const uint8_t s_AcceptTransfer   = 8; // destination account accepts an incoming transfer
    };

    struct ListActions
    {
        static const uint8_t s_UpdateInfo        = 0; // update name/description
        static const uint8_t s_DeleteList        = 1; // delete list + all entries
        static const uint8_t s_AddAsset          = 2; // single-asset: add AssetID
        static const uint8_t s_RemoveAsset       = 3; // single-asset: remove AssetID
        static const uint8_t s_AddAssetGroup     = 4; // multi-asset: add/replace group
        static const uint8_t s_RemoveAssetGroup  = 5; // multi-asset: remove group by real AssetID
        static const uint8_t s_SetListManagers   = 6; // install list-specific signer set
        static const uint8_t s_ClearListManagers = 7; // remove list-specific signer set
    };

    // ----------------------------------------------------------------
    // Action payloads  (stored inline after Proposal header)
    // ----------------------------------------------------------------

    struct PayloadUpdateAccountInfo
    {
        uint8_t  m_nNameLen;
        uint8_t  m_nWebsiteLen;
        uint16_t m_nDescLen;
        // followed by: name[], website[], desc[]
    };

    struct PayloadAddSigner
    {
        PubKey  m_pkNew;
        uint8_t m_NewThreshold;
    };

    struct PayloadRemoveSigner
    {
        uint8_t m_SignerIdx;
        uint8_t m_NewThreshold;
    };

    // AccountActions::s_CreateList
    // When proposing, Env::AddSig(m_pkList) is called to prove control of the list key.
    struct PayloadCreateList
    {
        PubKey   m_pkList;
        uint8_t  m_ListType;  // ListType::s_Single or ListType::s_Multi
        uint8_t  m_nNameLen;
        uint16_t m_nDescLen;
        // followed by: name[], desc[]
    };

    struct PayloadUpdateListInfo
    {
        uint8_t  m_nNameLen;
        uint16_t m_nDescLen;
        // followed by: name[], desc[]
    };

    // ListActions::s_AddAsset / s_RemoveAsset  (single-asset lists)
    struct PayloadAsset
    {
        AssetID m_Aid;
    };

    // ListActions::s_AddAssetGroup  (multi-asset lists)
    // Replaces the entire fake-ID array for m_RealAid (upsert semantics).
    struct PayloadAddAssetGroup
    {
        AssetID m_RealAid;
        uint8_t m_nFakes;
        // followed by: AssetID fakeIds[m_nFakes]
    };

    // ListActions::s_RemoveAssetGroup  (multi-asset lists)
    struct PayloadRemoveAssetGroup
    {
        AssetID m_RealAid;
    };

    // ListActions::s_SetListManagers
    // payload IS a SignerSet (header + PubKeys[])

    // AccountActions::s_WithdrawBalance
    struct PayloadWithdrawBalance
    {
        PubKey  m_pkRecipient; // who will receive the BEAM via ClaimBalance
        Amount  m_Amount;      // how much to designate (must be <= AccountBalance)
    };

    // AccountActions::s_InitiateTransfer
    struct PayloadInitiateTransfer
    {
        PubKey  m_pkAccountDest; // destination account that must accept
        PubKey  m_pkList;        // list being transferred
    };

    // AccountActions::s_CancelTransfer
    struct PayloadCancelTransfer
    {
        PubKey  m_pkList; // list whose pending transfer to cancel
    };

    // AccountActions::s_AcceptTransfer  (proposed on the DESTINATION account)
    struct PayloadAcceptTransfer
    {
        PubKey  m_pkAccountSrc; // source account that initiated the transfer
        PubKey  m_pkList;       // list being accepted
    };

    // ----------------------------------------------------------------
    // Methods
    // ----------------------------------------------------------------

    namespace Method
    {
        struct Init
        {
            static const uint32_t s_iMethod = 0;
            Upgradable3::Settings m_Upgradable;
            Settings m_Settings;
        };

        // Create account + initial signer set. Requires Env::AddSig(m_pkAccount).
        // Fee forwarded to DaoVault immediately.
        //
        // Variable tail: PubKey signers[m_nSigners], name[], website[], desc[]
        struct CreateAccount
        {
            static const uint32_t s_iMethod = 3;
            PubKey   m_pkAccount;
            uint8_t  m_Threshold;
            uint8_t  m_nSigners;
            uint8_t  m_nNameLen;
            uint8_t  m_nWebsiteLen;
            uint16_t m_nDescLen;
            // followed by: PubKey signers[m_nSigners], name[], website[], desc[]
        };

        // Submit an account-scoped proposal. Requires Env::AddSig(m_pkProposer).
        // For action s_CreateList: also requires Env::AddSig(payload.m_pkList).
        // If m_pkProposer is a member of the account's signer set their vote is
        // cast automatically; if that vote alone reaches the threshold the action
        // executes in the same transaction (single-signer fast path).
        struct ProposeAccountAction
        {
            static const uint32_t s_iMethod = 4;
            PubKey  m_pkAccount;
            PubKey  m_pkProposer;
            uint8_t m_Action; // AccountActions::*
            // followed by action payload
        };

        // Vote on an account proposal. Requires Env::AddSig(signers[m_SignerIdx]).
        // Auto-executes on yes-threshold; auto-rejects on no-threshold.
        // Expired proposals cannot be voted on.
        struct VoteAccountProposal
        {
            static const uint32_t s_iMethod = 5;
            PubKey   m_pkAccount;
            uint32_t m_ProposalID;
            uint8_t  m_SignerIdx;
            uint8_t  m_bYes; // 1 = yes, 0 = no
        };

        // Cancel an account proposal. Only the original proposer can cancel.
        // Requires Env::AddSig(m_pkProposer).
        struct CancelAccountProposal
        {
            static const uint32_t s_iMethod = 6;
            PubKey   m_pkAccount;
            uint32_t m_ProposalID;
            PubKey   m_pkProposer;
        };

        // Submit a list-scoped proposal. Requires Env::AddSig(m_pkProposer).
        // Uses list managers if set, otherwise falls back to account signers.
        // Same auto-vote/fast-path rules as ProposeAccountAction.
        struct ProposeListAction
        {
            static const uint32_t s_iMethod = 7;
            PubKey   m_pkAccount;
            PubKey   m_pkList;
            PubKey   m_pkProposer;
            uint8_t  m_Action; // ListActions::*
            // followed by action payload
        };

        // Vote on a list proposal. Requires Env::AddSig(signers[m_SignerIdx]).
        struct VoteListProposal
        {
            static const uint32_t s_iMethod = 8;
            PubKey   m_pkAccount;
            PubKey   m_pkList;
            uint32_t m_ProposalID;
            uint8_t  m_SignerIdx;
            uint8_t  m_bYes;
        };

        // Cancel a list proposal. Only the original proposer can cancel.
        // Requires Env::AddSig(m_pkProposer).
        struct CancelListProposal
        {
            static const uint32_t s_iMethod = 9;
            PubKey   m_pkAccount;
            PubKey   m_pkList;
            uint32_t m_ProposalID;
            PubKey   m_pkProposer;
        };

        // Delete an expired proposal. Anyone can call; no signature required.
        struct CleanupProposal
        {
            static const uint32_t s_iMethod = 10;
            uint8_t  m_bAccountProposal; // 1 = account proposal, 0 = list proposal
            PubKey   m_pkAccount;
            PubKey   m_pkList;           // ignored when m_bAccountProposal == 1
            uint32_t m_ProposalID;
        };

        // Collect BEAM approved by a s_WithdrawBalance proposal execution.
        // The designated recipient builds a transaction and calls this method.
        // Requires Env::AddSig(m_pkRecipient). No voting needed — approval
        // was already granted when the proposal executed.
        struct ClaimBalance
        {
            static const uint32_t s_iMethod = 11;
            PubKey  m_pkRecipient; // must match PendingClaim key
            PubKey  m_pkAccount;   // source account (identifies which PendingClaim)
        };

        // Delete an expired PendingTransfer record. Anyone can call; no sig required.
        struct CleanupTransfer
        {
            static const uint32_t s_iMethod = 12;
            PubKey  m_pkAccount; // source account of the transfer
            PubKey  m_pkList;
        };

    } // namespace Method

    // ----------------------------------------------------------------
    // TODO: future multi-sig / governance extensions
    //
    // 1. Weighted voting
    //    Each signer carries a weight; proposals pass when total yes-weight
    //    >= configured weight_threshold instead of a flat vote count.
    //
    // 2. Per-action-type TTL floors
    //    Sensitive operations (DeleteAccount, RemoveSigner) enforce a longer
    //    minimum TTL so other signers have more time to cast a veto.
    //
    // 3. Signer rotation quorum lock
    //    Prevent reducing N below a floor value, so a compromised signer
    //    cannot unilaterally strip all co-signers.
    //
    // 4. Cross-account list transfer
    //    A proposal that moves a list from one account to another, requiring
    //    M-of-N approval from both source and destination accounts.
    // ----------------------------------------------------------------

#pragma pack(pop)

} // namespace AssetLists
