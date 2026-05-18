---
eip: TBD
title: FOCIL Eligibility Profile
description: Five-class FOCIL eligibility profile across legacy, AA, private-AA, and witness-bearing frame transactions
author: Thomas Thiery (@soispoke)
discussions-to: TBD
status: Draft
type: Standards Track
category: Core
created: 2026-05-15
requires: 7805, 7928, 8141
---

## Abstract

Defines five FOCIL eligibility classes that span the non-blob transaction types Ethereum is shipping: legacy and EIP-7702 transactions (Class 1), AA frame transactions (Class 2), AA frame transactions with keyed nonces for privacy protocols (Class 3), frame transactions that carry bounded storage witnesses for state outside AA-VOPS (Class 4), and transactions that are not FOCIL-eligible including blob transactions (Class 5). Class 1 reads the VOPS schema (`address`, `nonce`, `balance`, `codeFlag`) from the [VOPS proposal](https://ethresear.ch/t/a-pragmatic-path-towards-validity-only-partial-statelessness-vops/22236); Classes 2-4 extend it with bounded AA-VOPS state, keyed nonces, recent root references, and witnesses. For each FOCIL-eligible class the EIP specifies the validation reads, eligibility conditions, per-IL VERIFY budget, and index-based omission check. Mempool admission and FOCIL eligibility are treated as separate policies.

## Motivation

FOCIL enforces inclusion when an includer can list a transaction and an attester can later check that omission was unjustified. As Ethereum adds frame transactions, keyed nonces, recent root references, and witness-bearing validation, FOCIL's public validity surface fragments unless the eligibility rules are unified. This EIP defines the rules once, ties each new transaction type to a bounded validation surface, and makes the per-class FOCIL guarantee explicit.

## Specification

The key words "MUST", "MUST NOT", "REQUIRED", "SHOULD", and "MAY" are to be interpreted as described in RFC 2119 and RFC 8174.

This specification requires [EIP-7805](./eip-7805.md), [EIP-7928](./eip-7928.md), [EIP-8141](./eip-8141.md), the Keyed Nonces frame-transaction extension ([[drafts/keyed-nonces/eip-draft]]), and the Recent Root References frame-transaction extension ([[drafts/recent-root-references/eip-draft|Recent Root References]]).

### Mempool eligibility and FOCIL eligibility are separate policies

Public mempool admission and FOCIL eligibility apply different rules to the same transaction:

```text
public-mempool admission:
  gossip-layer policy
  conservative VERIFY simulation cap to protect node resources
  not consensus

FOCIL eligibility:
  protocol-level inclusion guarantee
  per-IL VERIFY budget specified here
  enforced by attesters via the omission check
```

A transaction MAY be public-mempool-admissible without being FOCIL-eligible (Class 5 reaches builders but FOCIL does not enforce inclusion). A transaction MAY be FOCIL-eligible without being public-mempool-admissible (custom mempool or direct submission, see §[Includer admission](#includer-admission)). Includers listing FOCIL-eligible transactions MUST NOT rely on default-mempool admission as a precondition.

### Constants

| Name | Value |
|---|---:|
| `MAX_VERIFY_GAS_PER_IL` | `2**20` |
| `MAX_VERIFY_GAS_PER_TX` | `TBD` |
| `AA_VOPS_SLOT_COUNT` | `4` |
| `MAX_WITNESS_BYTES_PER_TX` | `TBD` |
| `MAX_WITNESSED_READS_PER_TX` | `TBD` |

`MAX_VERIFY_GAS_PER_TX` MAY differ from any default public-mempool simulation cap; the two are independent policies (see above).

### Eligibility classes

A transaction belongs to exactly one of the following FOCIL classes:

| Class | Transaction shape | Adds to validation reads |
|---|---|---|
| 1: VOPS (pre-AA) | Non-frame, non-blob transactions (legacy / 2930 / 1559 / 7702) | — |
| 2: AA-VOPS (post-AA) | EIP-8141 frame tx, `nonce_key == 0` | storage slots, code, recent root references |
| 3: Private AA-VOPS | EIP-8141 frame tx, `nonce_key != 0`, `nonce_seq == 0` | keyed-nonce state |
| 4: Witness-profile | Class 2 or 3 with declared storage witnesses | bounded witnesses and BAL-derived freshness checks |
| 5: Not FOCIL-eligible | Blob transactions (EIP-4844 / type 3) and any other ineligible transaction | (FOCIL does not enforce) |

Blob transactions are excluded by design: blob scheduling is governed by a separate target/max blob-count mechanism that does not compose with inclusion-list enforcement. Coupling blob slots to FOCIL would tie two independent scheduling regimes.

Classes 1-4 are FOCIL-eligible. This EIP specifies the validation rules, validation reads, and budgets for each. Class 5 transactions are not FOCIL-enforced; the builder MAY include them under normal block-construction rules and any FOCIL omission of them is excused.

### Class 1: VOPS (pre-AA)

A transaction is Class 1 eligible if all of the following hold:

1. It is a valid post-fork non-frame, non-blob transaction (legacy, EIP-2930, EIP-1559, or EIP-7702).
2. Its signature validates against the recovered sender.
3. `sender.nonce == tx.nonce` at the claimed block index.
4. `sender.balance >= tx.max_fee_per_gas * tx.gas_limit + tx.value` at the claimed block index.
5. `tx.chain_id` matches.

Validation reads (VOPS state, per [the VOPS proposal](https://ethresear.ch/t/a-pragmatic-path-towards-validity-only-partial-statelessness-vops/22236)): `address`, `nonce`, `balance`, `codeFlag` for `sender` and `payer` (if distinct).

Class 1 transactions do not consume `MAX_VERIFY_GAS_PER_IL`. Their validation work is bounded by the signature check and the VOPS-field read.

For senders with `codeFlag == 1` (EIP-7702 delegated accounts), the per-IL constraint described under [Includer admission](#includer-admission) restricts to at most one pending Class 1 transaction per `sender` per inclusion list, to prevent nonce or balance conflicts within the IL.

### Class 2: AA-VOPS (post-AA)

A transaction is Class 2 eligible if all of the following hold:

1. It is a valid post-fork EIP-8141 frame transaction with `nonce_key == 0`.
2. Its leading validation prefix contains only `VERIFY` frames before the first non-`VERIFY` frame.
3. The validation prefix executes a payment-scoped `APPROVE`.
4. The validation prefix reads only Class 2 state.
5. The validation prefix consumes no more than `MAX_VERIFY_GAS_PER_TX`.

Class 2 validation reads (AA-VOPS state, extending the VOPS schema):

- VOPS fields (`address`, `nonce`, `balance`, `codeFlag`) for `sender` and `payer`;
- code for `sender` and `payer`, resolved through the account `codeHash` or equivalent code identifier if that identifier is not already part of the VOPS account record;
- the first `AA_VOPS_SLOT_COUNT` storage slot values of `sender` and `payer`, if those slots are declared by the transaction and held in AA-VOPS;
- `storageRoot` only when a declared slot value is supplied or checked by proof rather than already held in AA-VOPS;
- precompiles and constants needed to verify validation logic;
- recent root references per [[drafts/recent-root-references/eip-draft|Recent Root References]].

Any other dynamic storage read makes the transaction ineligible under Class 2.

### Class 3: AA-VOPS for private frame transactions

A transaction is Class 3 eligible if all of the following hold:

1. It is a valid post-fork EIP-8141 frame transaction.
2. It uses `nonce_key != 0` and `nonce_seq == 0`.
3. Its leading validation prefix contains only `VERIFY` frames before the first non-`VERIFY` frame.
4. The validation prefix executes a payment-scoped `APPROVE`.
5. A spend-authorizing proof, or a proof sufficient to authorize the selected `nonce_key`, is verified before that `APPROVE`.
6. The selected keyed nonce is consumed by that `APPROVE`.
7. The validation prefix reads only Class 3 state.
8. The validation prefix consumes no more than `MAX_VERIFY_GAS_PER_TX`.

Class 3 validation reads:

- all Class 2 reads (including recent root references for the privacy protocol's commitment-tree root and any related recent-root dependencies);
- keyed-nonce state for `(sender, nonce_key)`.

The privacy protocol's ordinary execution state (commitment-tree internals, nullifier mappings, arbitrary privacy-contract storage) MUST NOT be read during the validation prefix.

The selected `nonce_key` SHOULD be a domain-separated value bound to the privacy protocol's nullifier or equivalent single-use spend identifier.

### Class 4: Witness-profile

A transaction is Class 4 eligible if it satisfies the structural conditions of Class 2 or Class 3 AND brings bounded storage witnesses for every validation read outside Class 2/3 state. Witnesses MUST satisfy:

1. The number of witnessed reads is at most `MAX_WITNESSED_READS_PER_TX`.
2. The total witness byte size is at most `MAX_WITNESS_BYTES_PER_TX`.
3. Each witness proves inclusion against a state root that is itself either a recent root reference (per [[drafts/recent-root-references/eip-draft|Recent Root References]]) or a system-level historical state root pinned to a specific block `N`.
4. Witnessed values MUST NOT determine further validation reads; the read set is fully declared up front.
5. Witness verification cost is accounted in the validation prefix gas total.
6. The transaction declares its outside-VOPS dependencies as a list `D` of `(address, storage_key)` pairs covered by the witness. The witness is pinned to a specific block `N`. The transaction is freshness-admissible only if `last_touched(d) <= N` for every `d in D`, where `last_touched` is the most recent block at which `d` was written, derived from BAL write history for blocks `N+1` through the current head.

Class 4 validation reads: Class 2 or Class 3 reads (depending on whether `nonce_key == 0` or `!= 0`), plus the declared witnessed reads.

The freshness check in condition 6 is independent of the cryptographic witness verification (which remains stable against the historical root). Mempool nodes maintain a rolling inverted index `last_touched[(address, storage_key)] -> block` over recent BAL-derived writes; freshness lookup is O(1) per declared dependency. The depth of the retained write-history window caps the maximum admissible witness age. BALs can help a node follow written entries forward at the head, but they do not supply values for unmodified state and do not by themselves provide rollback data. When a tracked dependency is touched by a newly accepted block, the affected pending transaction is not silently revalidated by the mempool: the wallet MUST regenerate the witness against a more recent anchor and re-sign the envelope, or the transaction is evicted.

### Class 5: Not FOCIL-eligible

The following are not FOCIL-eligible under this profile:

- Blob transactions (EIP-4844, type 3). Blob scheduling is governed by separate target / max blob-count limits and does not compose with FOCIL inclusion-list enforcement.
- Any transaction that fails the eligibility conditions of Classes 1-4 (arbitrary state reads outside the allowed surface, exceeds `MAX_VERIFY_GAS_PER_TX`, non-deterministic validation, etc.).

The builder MAY include Class 5 transactions under normal block-construction rules. FOCIL does not enforce their inclusion and any FOCIL omission of them is excused.

### Includer admission

A FOCIL includer MAY include a Class 1-4 eligible transaction from any submission path: default public mempool, custom mempool (for example a privacy mempool that carries Class 3 transactions), or direct submission. A transaction does not need default-mempool admission to be FOCIL-eligible.

For each inclusion list, the includer processes Class 2-4 eligible transactions in list order. The sum of validation-prefix gas for Class 2-4 eligible transactions in one inclusion list MUST NOT exceed `MAX_VERIFY_GAS_PER_IL`. Class 1 transactions do not consume from `MAX_VERIFY_GAS_PER_IL`.

For senders with `codeFlag == 1` (EIP-7702 delegated accounts), at most one Class 1 transaction per `sender` MAY appear in a given inclusion list. This prevents nonce or balance conflicts among multiple pending transactions from the same delegated account and matches the VOPS-level admission rule. Pure EOAs (`codeFlag == 0`) are not subject to this per-sender per-IL cap.

A Class 1-4 eligible transaction that exceeds `MAX_VERIFY_GAS_PER_TX` or that does not fit the remaining `MAX_VERIFY_GAS_PER_IL` budget at its IL position is not FOCIL-enforceable.

### Omission check (index-based)

For each omitted Class 1-4 eligible transaction, the builder provides a block index at which it claims the transaction was attempted.

Attesters evaluate state at that index using locally available state plus [[concepts/BAL|EIP-7928]] post-state updates, client state-diff data, or an equivalent authenticated state-diff mechanism. BAL post-state updates can advance written entries forward. Reconstructing earlier values, including across reorgs, requires pre-state diffs or cached pre-values.

An omitted Class 1-4 transaction is an unexcused FOCIL violation only if all of the following hold at the claimed index:

1. the transaction passes static EIP-8141 and fee sanity checks;
2. all class-specific eligibility conditions hold (Class 2-3: `nonce_key` and proof-before-`APPROVE` structure; Class 3: selected keyed nonce unused; Class 4: declared witnesses match reconstructed roots);
3. all class-permitted validation reads are available and match the reconstructed state;
4. the validation prefix (or signature check, for Class 1) succeeds;
5. the transaction fits the remaining gas, the per-tx VERIFY budget, and the IL VERIFY budget at the claimed index.

Failure of any condition excuses omission. The index-based check is linear in the number of included eligibility checks rather than quadratic in omitted transaction orderings; this is the design basis for `MAX_VERIFY_GAS_PER_IL = 2**20`.

### AA-VOPS state profile

The minimal cumulative state required to serve FOCIL eligibility for Classes 1-4 is:

```text
VOPS baseline (Class 1):
  address, nonce, balance, codeFlag
    for any FOCIL-eligible sender or payer

AA-VOPS extension (Classes 2-4):
  + code for any contract used as sender or payer
  + codeHash or equivalent code identifier, if not already present
    in the VOPS account record used to authenticate that code
  + first AA_VOPS_SLOT_COUNT storage slot values of any such account,
    where slots are declared by the transaction
  + storageRoot only when declared slot values are supplied or checked by proof
  + keyed-nonce state (system contract, for Class 3)
  + recent root state for any source identifier that may be referenced
  + small pending-conflict overlays

Recent update and freshness history:
  + rolling last_touched[(address, storage_key)] -> block index,
    derived from recent BAL writes, for Class 4 witness freshness
  + optional BAL-derived post-state updates for written VOPS / AA-VOPS entries
  + rollback data from client state diffs or cached pre-values
```

The VOPS schema (4 fields per account, ~40 bytes) is the baseline. AA-VOPS adds bounded storage-slot values and code, keeping the state surface small while supporting frame-transaction validation. Witness-profile transactions (Class 4) reach state outside AA-VOPS only through the witnesses they carry; AA-VOPS nodes verify the witness against the referenced root and use recent BAL-derived write history to reject stale witnesses.

BAL-derived history is a freshness and head-tracking aid, not arbitrary state storage. It covers recent writes, and any touched-key metadata a BAL exposes; state that was not modified does not appear with a value and must be supplied from held AA-VOPS state or from a witness. A node that updates local AA-VOPS state from BAL post-state values MUST retain enough pre-state information, either through the execution client's normal state-diff machinery or by caching pre-values before overwriting, to roll back across reorgs.

AA-VOPS nodes are not required to store full privacy-protocol state, full nullifier sets, full commitment trees, or arbitrary contract storage. Partial-statelessness nodes that hold additional storage (per the VOPS proposal's AA-VOPS variant) are a superset and remain compatible.

### Engine API

The execution layer MUST receive enough ordered inclusion-list data through `newPayload` and `forkchoiceUpdated` to perform the per-class eligibility check at any claimed block index and to return omission-excuse information. The exact Engine API extensions are left to the FOCIL integration EIP.

## Rationale

A unified eligibility profile across transaction types prevents validation drift between includers, attesters, and partial-state nodes. Naming five distinct classes makes the public validity surface explicit: legacy transactions occupy the smallest surface, witness-bearing transactions the largest, and everything in between binds to AA-VOPS plus the named restricted state types (keyed nonces, recent root references).

Separating mempool admission from FOCIL eligibility lets the default public mempool stay conservative (small simulation cap, gossip resource protection) while FOCIL continues to enforce inclusion for Classes 1-4 through any submission path. Privacy protocols use this separation in particular: a privacy mempool can carry Class 3 transactions that exceed the default mempool's cap, and FOCIL still enforces inclusion via the per-IL budget.

The `2**20` per-IL VERIFY budget follows the index-based FOCIL-AA design: attester work is linear in the number of included eligibility checks rather than quadratic in omitted transaction orderings. The value is large enough for proof-heavy privacy validation while remaining bounded at the inclusion-list layer.

Witness-profile eligibility extends FOCIL beyond the AA-VOPS surface without forcing every includer or attester to hold arbitrary state. The witness format and verification work are bounded; the witness's state root MUST be itself a FOCIL-checkable object (recent root reference or system-level historical root) so attesters can verify witnesses without trust.

## Security Considerations

Class boundaries are validity conditions, not optimizations. A transaction that claims a class but reads state outside that class's surface is not FOCIL-enforced under this profile, and its omission is excused.

For Class 3, applications MUST bind the spend-authorizing proof to at least the sender, `nonce_key`, `nonce_seq == 0`, chain ID, and the relevant frame commitment. A stale recent root is valid only while its recent root reference remains inside the buffer's window; larger windows improve propagation tolerance but increase revocation latency and stale-root surface.

For Class 4, witnesses that depend on unconfirmed state roots are not FOCIL-enforced: the referenced root MUST be a finalized recent root entry or a system historical root. Witness-bearing transactions that allow witnessed values to direct further reads are not eligible; the read set MUST be fully declared up front to keep attester work bounded.

Class 4 freshness depends on the BAL retention window the mempool node maintains. A witness pinned to block `N` is admissible only while the node holds BALs for blocks `N+1` through the current head; if blocks before the node's earliest retained BAL fall between the witness anchor and the current head, the node cannot prove the dependencies stayed untouched and MUST treat the transaction as freshness-stale. Nodes SHOULD retain BAL coverage at least as deep as their accepted witness age. Wallets generating Class 4 witnesses SHOULD anchor to blocks recent enough that the corresponding BALs are widely retained.

The Class 4 freshness check assumes BAL completeness: every state mutation between the witness block and the current head MUST appear in BALs, including system-contract writes (recent-root entries, keyed-nonce consumption). Implementations MUST NOT exempt system-contract writes from BAL coverage; doing so produces a freshness false-positive where the witness is accepted despite a touched dependency.

Class 4 transactions publicly declare their outside-VOPS dependencies. For applications wanting to keep their state-access pattern private, this is a deanonymization surface. Privacy spends SHOULD use Class 3 (recent root references plus keyed nonces, no declared dependencies) wherever possible. Class 4 is best suited to AA-style wallets reading their own ancillary state, where the dependency set is intrinsically public.

Outside-VOPS dependencies on high-churn state are mempool-hostile under Class 4: every touch of a declared dependency forces the wallet to regenerate the witness and re-sign the envelope. Applications that route through public mempool channels SHOULD keep Class 4 dependency sets confined to low-churn state such as authorization roots, fee schedules, or governance-controlled parameters.

Subjective filters such as Bloom filters over spent keys MAY be used for local mempool triage. They are not consensus validity and MUST NOT replace exact keyed-nonce checks, exact recent root reference checks, or exact witness verification during omission validation.

Post-quantum signature or proof schemes are FOCIL-eligible under this profile only if their validation prefix fits `MAX_VERIFY_GAS_PER_TX` and `MAX_VERIFY_GAS_PER_IL`. Otherwise their omission is excused until aggregation or proof-verification costs change.

## Copyright

Copyright and related rights waived via [CC0](/LICENSE).