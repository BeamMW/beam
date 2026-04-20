# AssetLists Contract

A Beam BVM shader that lets users register on-chain accounts and publish curated named lists of Beam Asset IDs. All list data is pure metadata — no tokens are locked. Account and list creation carry a non-refundable BEAM fee forwarded to the DAO Vault to deter spam.

The contract is governed by M-of-N multi-sig at both account and list level, using a multi-transaction proposal/vote system suited to dapp UIs.

---

## Core Concepts

### Account

An account is an on-chain identity anchored by a dedicated public key (`pkAccount`). It stores a profile (name, website, description) and owns one or more lists. Every account has a **SignerSet** — an M-of-N set of public keys whose approval is required for any account-level or list-level change.

Accounts accumulate a BEAM balance from non-signer proposal fees. This balance can be withdrawn via the `s_WithdrawBalance` proposal action (M-of-N governed).

### List

A list belongs to an account and is identified by its own public key (`pkList`). Lists come in two types, fixed at creation:

| Type | Description | Use case |
|---|---|---|
| **Single** | Flat sequence of Asset IDs | Approved assets, watchlists, blocklists |
| **Multi** | Mapping of `realAssetID → [fakeAssetID, ...]` | Bridge/imposter registries where one canonical asset maps to several equivalent representations |

Lists optionally carry their own **ListSigners** (a separate M-of-N set). When no ListSigners entry is present, the parent account's SignerSet governs the list.

Lists can be transferred to another account via the `s_InitiateTransfer` / `s_AcceptTransfer` handshake (both require M-of-N approval from the respective accounts).

### Multi-sig (M-of-N)

All mutable operations on an account or list go through a proposal/vote cycle:

1. **Propose** — any party submits a proposal for a specific action. If the proposer is a member of the governing SignerSet, their yes-vote is auto-cast. If that single vote already reaches the threshold the action executes immediately (single-signer fast path — no second transaction needed).
2. **Vote** — each signer casts yes or no on an open proposal.
3. **Auto-execute / auto-reject** — the contract executes the action as soon as `yes_count >= M`, or discards the proposal as soon as `no_count >= N - M + 1` (reject threshold).
4. **Cancel** — only the original proposer may cancel an open proposal before it resolves.
5. **Cleanup** — anyone may delete an expired proposal (height > `m_hExpire`) to reclaim contract storage.

Proposals are scoped: **account proposals** govern the account and its signer set; **list proposals** govern a specific list.

### Non-signer Proposal Fee

When a party who is **not** a member of the governing SignerSet submits a proposal, they must pay `m_FeeProposal` groth. The fee is split 50/50: half goes directly to the DAO Vault, half accumulates in the target account's on-chain balance. Setting `m_FeeProposal = 0` disables the fee entirely.

---

## Permission Summary

### Anyone (no signature required)

| Action | Method |
|---|---|
| Create an account (with account key sig) | `CreateAccount` (Method 3) |
| Propose any account or list action | `ProposeAccountAction` / `ProposeListAction` |
| Vote on an open proposal (requires signer key) | `VoteAccountProposal` / `VoteListProposal` |
| Delete an **expired** proposal | `CleanupProposal` (Method 10) |
| Delete an **expired** pending transfer | `CleanupTransfer` (Method 12) |

> Note: Non-signer proposers pay `m_FeeProposal` (if non-zero). Proposals from non-signers have no auto-vote — they still require M yes-votes from the actual signers.

### Account Signer (holds a key in the account's SignerSet)

All of the above (fee-free), plus:

| Action | Proposal type | Notes |
|---|---|---|
| Update account profile (name, website, description) | Account | `AccountActions::s_UpdateInfo` |
| Add a signer to the account SignerSet + set new threshold | Account | `AccountActions::s_AddSigner` |
| Remove a signer from the account SignerSet + set new threshold | Account | `AccountActions::s_RemoveSigner`; cannot remove last signer |
| Delete the account | Account | `AccountActions::s_DeleteAccount`; all lists must be deleted first |
| Create a new list | Account | `AccountActions::s_CreateList`; also requires sig of new `pkList` |
| Withdraw accumulated balance to a designated recipient | Account | `AccountActions::s_WithdrawBalance`; creates a `PendingClaim` record |
| Initiate a list transfer to another account | Account | `AccountActions::s_InitiateTransfer`; creates a `PendingTransfer` record |
| Cancel a pending list transfer | Account | `AccountActions::s_CancelTransfer` |
| Accept an incoming list transfer (on destination account) | Account | `AccountActions::s_AcceptTransfer`; migrates all list storage |

### List Signer (holds a key in the list's effective SignerSet)

The effective SignerSet for a list is the list's own ListSigners if present, otherwise the account's SignerSet.

| Action | Proposal type | Constraint |
|---|---|---|
| Update list profile (name, description) | List | `ListActions::s_UpdateInfo` |
| Add an Asset ID to a **single** list | List | `ListActions::s_AddAsset` — list type must be `single` |
| Remove an Asset ID from a **single** list | List | `ListActions::s_RemoveAsset` — list type must be `single` |
| Add or replace an asset group in a **multi** list | List | `ListActions::s_AddAssetGroup` — list type must be `multi`; upsert semantics |
| Remove an asset group from a **multi** list | List | `ListActions::s_RemoveAssetGroup` — list type must be `multi` |
| Install list-specific managers (ListSigners) | List | `ListActions::s_SetListManagers` |
| Remove list-specific managers | List | `ListActions::s_ClearListManagers` |
| Delete the list and all its entries | List | `ListActions::s_DeleteList` |

### Designated Recipient

| Action | Method | Notes |
|---|---|---|
| Claim an approved balance withdrawal | `ClaimBalance` (Method 11) | Requires sig of `m_pkRecipient`; consumes `PendingClaim` record |

---

## Methods

| # | Name | Caller / Sig required | Description |
|---|---|---|---|
| 0 | `Init` | Contract deployer | Deploy contract, set DAO Vault CID, creation fees, proposal fee, and proposal TTL |
| 3 | `CreateAccount` | `sig(pkAccount)` | Register account + initial signer set; fee sent to DAO Vault |
| 4 | `ProposeAccountAction` | `sig(pkProposer)` | Submit a proposal for an account-scoped action; non-signers pay `m_FeeProposal` |
| 5 | `VoteAccountProposal` | `sig(signers[idx])` | Cast yes/no on an account proposal |
| 6 | `CancelAccountProposal` | `sig(original proposer)` | Withdraw an open account proposal |
| 7 | `ProposeListAction` | `sig(pkProposer)` | Submit a proposal for a list-scoped action; non-signers pay `m_FeeProposal` |
| 8 | `VoteListProposal` | `sig(signers[idx])` | Cast yes/no on a list proposal |
| 9 | `CancelListProposal` | `sig(original proposer)` | Withdraw an open list proposal |
| 10 | `CleanupProposal` | Anyone (no sig) | Delete an expired proposal |
| 11 | `ClaimBalance` | `sig(pkRecipient)` | Recipient collects BEAM approved by a `s_WithdrawBalance` proposal |
| 12 | `CleanupTransfer` | Anyone (no sig) | Delete an expired `PendingTransfer` record |

Methods 1–2 are reserved for the `upgradable3` upgrade mechanism.

---

## Deployment

### Build

Compile both binaries with the shader-sdk WASM toolchain (Clang targeting wasm32):

```bash
make_shader.bat bvm/Shaders/asset_lists/contract.cpp
make_shader.bat bvm/Shaders/asset_lists/app.cpp
```

After the first successful build, record the `ShaderID` (hash of `contract.wasm`) and fill it into the `s_pSID[]` array in `contract.h`, then rebuild `app.wasm`. The app uses `s_pSID` for `EnumAndDumpContracts` and version verification at upgrade time.

### Init parameters (Method 0)

`Method::Init` embeds two sub-structs that must be populated together:

**`Upgradable3::Settings`** — upgrade governance:

| Field | Type | Description |
|---|---|---|
| `m_hMinUpgradeDelay` | `Height` | Minimum blocks between `ScheduleUpgrade` and the upgrade taking effect. A value of `0` allows immediate upgrades; use a non-zero value (e.g. 1440 ≈ 24 h) so co-admins can veto a rogue schedule. |
| `m_MinApprovers` | `uint32_t` | How many admin keys must co-sign each upgrade control transaction (M-of-N). Must be ≥ 1 and ≤ the number of non-zero entries in `m_pAdmin`. |
| `m_pAdmin[32]` | `PubKey[32]` | Up to 32 admin public keys. Unused slots stay zero. The wallet app auto-fills the deployer's key into the first free slot if it is not already present. |

**`AssetLists::Settings`** — contract fee config:

| Field | Type | Description |
|---|---|---|
| `m_cidDaoVault` | `ContractID` | DAO Vault contract to receive fee payments. Locked in at init via `Env::RefAdd`; cannot be changed after deployment. |
| `m_FeeAccount` | `Amount` (groth) | Non-refundable fee paid on `CreateAccount`, forwarded directly to the DAO Vault. |
| `m_FeeList` | `Amount` (groth) | Non-refundable fee charged when a `CreateList` proposal executes, forwarded to the DAO Vault. |
| `m_FeeProposal` | `Amount` (groth) | Fee paid by non-signer proposers; split 50/50 between the DAO Vault and the target account's balance. Set to `0` to disable. |
| `m_ProposalTtl` | `Height` | Default proposal lifetime in blocks (e.g. 1440 ≈ 24 h). Proposals older than this can be cleaned up by anyone via `CleanupProposal`. |

The `ContractID` is derived deterministically as `Hash(ShaderID || Init_args)`, so the same binary deployed with the same parameters always produces the same address.

---

## Upgradability

The contract uses the `upgradable3` pattern (Method 2). The live WASM bytecode can be replaced without touching any application state.

### Upgrade flow

1. **Schedule** — M-of-N admins co-sign a `ScheduleUpgrade` call (type 3 sub-type of Method 2). The call embeds the new `contract.wasm` bytecode and a target block height `hTarget`. The contract enforces `hTarget ≥ currentHeight + m_hMinUpgradeDelay`.

2. **Wait** — other admins have `m_hMinUpgradeDelay` blocks to inspect the scheduled bytecode. During this window a new `ScheduleUpgrade` call (also requiring M-of-N) can overwrite the pending upgrade, effectively vetoing it.

3. **Execute** — once `hTarget` is reached, *anyone* (no signature required) can finalize the swap by calling `ExplicitUpgrade` (type 1 sub-type of Method 2). The new WASM goes live and `OnUpgraded(prevVersion)` is invoked to perform any state migration.

### Admin key management (also Method 2)

Both operations below require M-of-N admin co-signatures, coordinated via the `MultiSigRitual` protocol in `upgradable3/app_common_impl.h`:

| Sub-type | Action |
|---|---|
| `ReplaceAdmin` (4) | Swap one entry in `m_pAdmin[]` — use for key rotation or removing a compromised admin. |
| `SetApprovers` (5) | Change the `m_MinApprovers` threshold — new value must remain in [1, active admin count]. |

### `s_pSID` and version verification

`contract.h` contains `s_pSID[]` — a list of known `ShaderID` values in version order. The wallet app checks the ShaderID of the bytecode you are about to schedule against this list to prevent accidentally deploying an unrecognized binary. Pass `bSkipVerifyVer = 1` to the `schedule_upgrade` app action only when deploying a build whose ShaderID has not yet been added to `s_pSID` (e.g. during development). Keep `s_pSID` up-to-date by appending each released ShaderID in version order.

---

## Technical Details

### Storage Layout

All keys share a flat contract key-value store, namespaced by a leading tag byte:

| Tag | Key | Value |
|---|---|---|
| `0` (Settings) | `uint8_t` | `State` — global fee config + account/list counts |
| `1` (Account) | `{tag, pkAccount}` | `Account` header + packed strings `[nName, name, nWeb, website, nDesc(u16), desc]` |
| `2` (AccountSigners) | `{tag, pkAccount}` | `SignerSet` header + `PubKey[nSigners]` |
| `3` (AccountProposal) | `{tag, pkAccount, proposalID(BE)}` | `Proposal` header + action payload |
| `4` (List) | `{tag, pkAccount, pkList}` | `List` header + packed strings `[nName, name, nDesc(u16), desc]` |
| `5` (ListSigners) | `{tag, pkAccount, pkList}` | `SignerSet` header + `PubKey[nSigners]` |
| `6` (ListProposal) | `{tag, pkAccount, pkList, proposalID(BE)}` | `Proposal` header + action payload |
| `7` (ListAsset) | `{tag, pkAccount, pkList, assetID}` | 1-byte dummy — presence = membership (single lists) |
| `8` (ListAssetGroup) | `{tag, pkAccount, pkList, realAssetID(BE)}` | `AssetGroup` header + `AssetID[nFakes]` (multi lists) |
| `9` (AccountBalance) | `{tag, pkAccount}` | `AccountBalance` — accumulated groth from non-signer proposal fees |
| `10` (PendingClaim) | `{tag, pkRecipient, pkAccount}` | `PendingClaim` — approved withdrawal amount awaiting `ClaimBalance` call |
| `11` (PendingTransfer) | `{tag, pkAccountSrc, pkList}` | `PendingTransfer` — in-progress list transfer offer from source to destination |

Proposal IDs are stored big-endian in keys so that `VarReader` range scans enumerate them in ascending numeric order.

### Variable-Length Values

Account and list values store a fixed-size header immediately followed by packed string bytes. Because `SaveVar_T` only writes `sizeof(T)` bytes, any update that patches header fields (e.g., incrementing `m_nAssets`) must read the full raw value into a buffer, patch the header in-place, and write back the full byte count — the string suffix must be preserved.

### Fees

| Event | Fee |
|---|---|
| `CreateAccount` | `m_FeeAccount` BEAM → DAO Vault (immediate, on account creation) |
| `CreateList` proposal executes | `m_FeeList` BEAM → DAO Vault |
| Non-signer `ProposeAccountAction` or `ProposeListAction` | `m_FeeProposal` groth: half → DAO Vault, half → target account balance |

Fees are non-refundable. The DAO Vault CID is locked in at contract init via `Env::RefAdd`.

### Balance Withdrawal Flow

1. Account signer proposes `s_WithdrawBalance` with a recipient public key and amount.
2. On M-of-N approval the contract deducts from `AccountBalance` and creates a `PendingClaim` record (additive — multiple approvals to the same recipient stack).
3. The designated recipient calls `ClaimBalance` (Method 11) — requires their signature — to receive the BEAM.

For a 1-of-1 account the propose and execute happen in one transaction (single-signer fast path).

### List Transfer Flow

1. Source account proposes `s_InitiateTransfer` naming the destination account and list. On approval a `PendingTransfer` record is created with a TTL.
2. Destination account proposes `s_AcceptTransfer` naming the source account and list. On approval all list storage is migrated to the destination's namespace: the list header (with `m_nProposals` reset to 0), any list-specific signers, all asset entries (single or multi). Stale list proposals on the source are discarded.
3. Either side can abandon: the source proposes `s_CancelTransfer`; the destination simply never accepts. Expired `PendingTransfer` records can be cleaned up by anyone via `CleanupTransfer` (Method 12).

`m_nLists` is unchanged globally — the list moves, it is not created or destroyed.

### Limits

| Constant | Value |
|---|---|
| Max signers per SignerSet | 8 (bitmask fits in `uint8_t`) |
| Max fake Asset IDs per multi-asset group | 16 |
| Account name max length | 64 bytes |
| Account website max length | 128 bytes |
| Account description max length | 256 bytes |
| List name max length | 64 bytes |
| List description max length | 256 bytes |

### Single-Signer Fast Path

When an account or list has threshold M = 1 (solo operation), a `ProposeXAction` call auto-casts the proposer's yes-vote and executes the action immediately — no separate `VoteXProposal` transaction is needed.

### Upgradability

The contract uses the `upgradable3` pattern. The upgrade governance (approver set, required approvals) is configured at init time via `Upgradable3::Settings`.

---

## Planned Extensions (TODO)

1. **Weighted voting** — signers carry individual weights; proposals pass when the cumulative yes-weight reaches a configured threshold.
2. **Per-action TTL floors** — sensitive operations (`DeleteAccount`, `RemoveSigner`) enforce a longer minimum proposal lifetime so co-signers have more time to veto.
3. **Signer rotation quorum lock** — prevent reducing N below a floor value so a compromised signer cannot unilaterally strip all co-signers.
