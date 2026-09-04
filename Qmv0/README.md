# Pi-Nexus Quantum Body v2

A quantum-resistant, multi-chain transaction engine — implemented in parallel across
**C++, Python, JSON, HTML/JS, and MQL5** — covering **Ethereum, Solana, Pi Network,
and Stellar/Soroban**.

## Why "quantum body"

Ethereum-style transactions are signed with ECDSA over secp256k1, which a
sufficiently large quantum computer running Shor's algorithm can break. This engine
replaces that signature layer with a **Lamport one-time signature (OTS)** scheme —
hash-based, so its security reduces only to SHA-256 preimage resistance, which Shor's
algorithm does not break. A **Merkle tree** aggregates many one-time keypairs into a
single reusable "quantum address," the same technique used by stateful hash-based
post-quantum standards like XMSS/SPHINCS+.

## Files

| File | Language | Role |
|---|---|---|
| `quantum_body_v2.hpp` / `.cpp` | C++17 | Reference engine: SHA-256 (self-contained), Lamport OTS, Merkle tree, multi-chain adapters. Compiles standalone, no deps. Includes a runnable demo `main()`. |
| `quantum_body_v2.py` | Python 3.8+ | Same engine, stdlib-only (`hashlib`). Powers the API below. |
| `api_blockchain.py` | Python / FastAPI | REST API: create identities, sign+broadcast transactions, verify signatures, seal blocks, per-chain explorer links. |
| `quantum_body_v2.json` | JSON Schema | Transaction schema, per-chain config, and a seeded demo dataset (`genesis_seed = 2025072607`) for cross-language testing. |
| `quantum_body_v2.html` | HTML/CSS/JS | Interactive dashboard: chain overview, seeded-demo explorer, an **in-browser** Lamport/Merkle signer (Web Crypto `SubtleCrypto`, nothing sent over the network), and a console to hit a running `api_blockchain.py` instance. Open directly in any browser. |
| `quantum_body_v2.mq5` | MQL5 | MetaTrader 5 Expert Advisor that polls the API for a quantum-signature-valid on-chain settlement as a confirmation gate, blended with an EMA momentum filter, before trading. |
| `logo.png` | — | Project mark: a Merkle tree rendered as a quantum node graph, violet-to-teal palette. |

## Running it

```bash
# C++
g++ -std=c++17 -O2 -o quantum_body quantum_body_v2.cpp && ./quantum_body

# Python engine demo
python3 quantum_body_v2.py

# REST API
pip install fastapi uvicorn --break-system-packages
uvicorn api_blockchain:app --reload --port 8000
# then open quantum_body_v2.html and use the API Console tab (default http://127.0.0.1:8000)

# MQL5
# Copy quantum_body_v2.mq5 into MQL5/Experts/, compile in MetaEditor,
# attach to a chart, and allow WebRequest for your API host in
# Tools > Options > Expert Advisors.
```

## Honest scope note

This is a from-scratch, auditable reference construction (Lamport OTS is a real,
decades-old hash-based signature scheme), not a wrapper around a vetted PQC library
like liboqs/Dilithium/Kyber, and it has not been security-audited. The chain adapters'
`broadcast()` methods are stubs — swap them for real `web3.py` / `solana-py` /
Pi Horizon / Soroban RPC calls before touching mainnet funds. Treat this as an
architecture and algorithm reference, not production custody software.
