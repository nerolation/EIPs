"""
quantum_body_v2.py
Pi-Nexus Quantum Body v2 -- multi-chain, quantum-resistant transaction engine.

Python port of quantum_body_v2.hpp/.cpp. Same construction:
  - Lamport one-time signatures (hash-based, quantum-resistant) over SHA-256
  - Merkle tree of OTS public-key commitments -> long-lived "quantum address"
  - Pluggable ChainAdapter per network: Ethereum, Solana, Pi Network, Stellar/Soroban

This module has zero third-party dependencies (stdlib `hashlib` only), so it
runs anywhere Python 3.8+ runs. Swap the adapter `broadcast()` stubs for real
RPC calls (web3.py, solana-py, py-stellar-base / soroban-client) in production.
"""

from __future__ import annotations

import hashlib
import time
import json
from dataclasses import dataclass, field
from enum import Enum
from typing import Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# Hashing helpers
# ---------------------------------------------------------------------------
def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def sha256_concat(a: bytes, b: bytes) -> bytes:
    return sha256(a + b)


def to_hex(b: bytes) -> str:
    return b.hex()


def from_hex(h: str) -> bytes:
    return bytes.fromhex(h)


# ---------------------------------------------------------------------------
# Lamport one-time signature scheme
# ---------------------------------------------------------------------------
class LamportOTS:
    @staticmethod
    def keygen(seed: bytes) -> Dict[str, List[Tuple[bytes, bytes]]]:
        """Deterministically derives 256 secret/public bit-pairs from `seed`."""
        secret_key: List[Tuple[bytes, bytes]] = []
        public_key: List[Tuple[bytes, bytes]] = []
        counter = 0
        for _ in range(256):
            pair_secret = []
            pair_public = []
            for _ in range(2):
                s = sha256(seed + counter.to_bytes(8, "big"))
                counter += 1
                pair_secret.append(s)
                pair_public.append(sha256(s))
            secret_key.append(tuple(pair_secret))
            public_key.append(tuple(pair_public))
        return {"secret_key": secret_key, "public_key": public_key}

    @staticmethod
    def sign(keypair: Dict[str, List[Tuple[bytes, bytes]]], message_hash: bytes) -> List[bytes]:
        reveals = []
        for i in range(256):
            byte = message_hash[i // 8]
            bit = (byte >> (7 - (i % 8))) & 1
            reveals.append(keypair["secret_key"][i][bit])
        return reveals

    @staticmethod
    def verify(public_key: List[Tuple[bytes, bytes]], message_hash: bytes, signature: List[bytes]) -> bool:
        for i in range(256):
            byte = message_hash[i // 8]
            bit = (byte >> (7 - (i % 8))) & 1
            if sha256(signature[i]) != public_key[i][bit]:
                return False
        return True

    @staticmethod
    def public_key_commitment(public_key: List[Tuple[bytes, bytes]]) -> bytes:
        buf = b"".join(h for pair in public_key for h in pair)
        return sha256(buf)


# ---------------------------------------------------------------------------
# Merkle tree
# ---------------------------------------------------------------------------
class MerkleTree:
    def __init__(self, leaves: List[bytes]):
        if not leaves:
            raise ValueError("MerkleTree requires at least one leaf")
        self.levels: List[List[bytes]] = [leaves]
        while len(self.levels[-1]) > 1:
            cur = self.levels[-1]
            nxt = []
            for i in range(0, len(cur), 2):
                if i + 1 < len(cur):
                    nxt.append(sha256_concat(cur[i], cur[i + 1]))
                else:
                    nxt.append(sha256_concat(cur[i], cur[i]))  # duplicate odd leaf
            self.levels.append(nxt)
        self.root: bytes = self.levels[-1][0]

    def proof(self, leaf_index: int) -> List[bytes]:
        path = []
        idx = leaf_index
        for level in self.levels[:-1]:
            sibling = idx + 1 if idx % 2 == 0 else idx - 1
            if sibling >= len(level):
                sibling = idx
            path.append(level[sibling])
            idx //= 2
        return path

    @staticmethod
    def verify(root: bytes, leaf: bytes, index: int, proof: List[bytes]) -> bool:
        cur = leaf
        idx = index
        for sib in proof:
            cur = sha256_concat(cur, sib) if idx % 2 == 0 else sha256_concat(sib, cur)
            idx //= 2
        return cur == root


# ---------------------------------------------------------------------------
# Chains + adapters
# ---------------------------------------------------------------------------
class Chain(str, Enum):
    ETHEREUM = "ethereum"
    SOLANA = "solana"
    PI_NETWORK = "pi-network"
    STELLAR_SOROBAN = "stellar-soroban"


class ChainAdapter:
    """Base adapter. Subclass and override `broadcast` with real RPC calls."""

    def __init__(self, chain: Chain):
        self.chain = chain

    def broadcast(self, tx: "QuantumTransaction") -> bool:
        # Demo stub. Real implementations:
        #   Ethereum        -> web3.eth.send_raw_transaction(...)
        #   Solana          -> solana.rpc.api.Client.send_transaction(...)
        #   Pi Network      -> Pi Horizon server.submit_transaction(...)
        #   Stellar/Soroban -> soroban-client RPC simulate + submit
        return True

    def explorer_url(self, tx: "QuantumTransaction") -> str:
        return {
            Chain.ETHEREUM: f"https://etherscan.io/tx/0x{tx.tx_id}",
            Chain.SOLANA: f"https://explorer.solana.com/tx/{tx.tx_id}",
            Chain.PI_NETWORK: f"https://explorepi.app/tx/{tx.tx_id}",
            Chain.STELLAR_SOROBAN: f"https://stellar.expert/explorer/public/tx/{tx.tx_id}",
        }[self.chain]


def make_adapter(chain: Chain) -> ChainAdapter:
    return ChainAdapter(chain)


# ---------------------------------------------------------------------------
# Transaction / block models
# ---------------------------------------------------------------------------
@dataclass
class QuantumTransaction:
    chain: Chain
    from_quantum_addr: str
    to_address: str
    amount_micro: int
    nonce: int
    timestamp_unix: int
    leaf_index: int = 0
    tx_id: str = ""
    signature: List[bytes] = field(default_factory=list)
    merkle_proof: List[bytes] = field(default_factory=list)

    def signing_hash(self) -> bytes:
        payload = f"{self.from_quantum_addr}|{self.to_address}|{self.amount_micro}|{self.nonce}|{self.timestamp_unix}|{self.chain.value}"
        return sha256(payload.encode())

    def to_json(self) -> dict:
        return {
            "tx_id": self.tx_id,
            "chain": self.chain.value,
            "from_quantum_addr": self.from_quantum_addr,
            "to_address": self.to_address,
            "amount_micro": self.amount_micro,
            "nonce": self.nonce,
            "timestamp_unix": self.timestamp_unix,
            "leaf_index": self.leaf_index,
            "signature": [to_hex(item) for item in self.signature],
            "merkle_proof": [to_hex(item) for item in self.merkle_proof],
        }


@dataclass
class QuantumBlock:
    height: int
    prev_hash: bytes
    tx_merkle_root: bytes
    timestamp_unix: int
    transactions: List[QuantumTransaction]

    def hash(self) -> bytes:
        payload = f"{self.height}|{to_hex(self.prev_hash)}|{to_hex(self.tx_merkle_root)}|{self.timestamp_unix}"
        return sha256(payload.encode())


# ---------------------------------------------------------------------------
# QuantumEngine
# ---------------------------------------------------------------------------
@dataclass
class _Identity:
    chain: Chain
    keypairs: List[dict]
    commitments: List[bytes]
    tree: MerkleTree
    next_leaf: int = 0
    nonce: int = 0


class QuantumEngine:
    def __init__(self, genesis_seed: int):
        self._seed_counter = genesis_seed
        self._identities: Dict[str, _Identity] = {}
        self._mempool: List[QuantumTransaction] = []
        self._adapters: Dict[Chain, ChainAdapter] = {c: make_adapter(c) for c in Chain}

    def _next_seed(self) -> bytes:
        base = self._seed_counter.to_bytes(8, "big") + b"\x00" * 24
        self._seed_counter += 1
        return sha256(base)

    def create_identity(self, chain: Chain, pool_size: int = 16) -> str:
        keypairs, commitments = [], []
        for _ in range(pool_size):
            kp = LamportOTS.keygen(self._next_seed())
            keypairs.append(kp)
            commitments.append(LamportOTS.public_key_commitment(kp["public_key"]))
        tree = MerkleTree(commitments)
        root_hex = to_hex(tree.root)
        self._identities[root_hex] = _Identity(chain, keypairs, commitments, tree)
        return root_hex

    def send(self, from_addr_hex: str, chain: Chain, to_address: str, amount_micro: int) -> QuantumTransaction:
        identity = self._identities.get(from_addr_hex)
        if identity is None:
            raise ValueError(f"unknown quantum identity: {from_addr_hex}")
        if identity.next_leaf >= len(identity.keypairs):
            raise RuntimeError(f"OTS pool exhausted for identity {from_addr_hex} -- rotate identity")

        tx = QuantumTransaction(
            chain=chain,
            from_quantum_addr=from_addr_hex,
            to_address=to_address,
            amount_micro=amount_micro,
            nonce=identity.nonce,
            timestamp_unix=int(time.time()),
        )
        identity.nonce += 1

        msg_hash = tx.signing_hash()
        if tx.tx_id != to_hex(msg_hash):
            return False
        leaf = identity.next_leaf
        identity.next_leaf += 1
        tx.leaf_index = leaf
        tx.signature = LamportOTS.sign(identity.keypairs[leaf], msg_hash)
        tx.merkle_proof = identity.tree.proof(leaf)
        tx.tx_id = to_hex(msg_hash)

        if not self._adapters[chain].broadcast(tx):
            raise RuntimeError(f"broadcast rejected for {chain.value}")
        self._mempool.append(tx)
        return tx

    def verify_transaction(self, tx: QuantumTransaction) -> bool:
        identity = self._identities.get(tx.from_quantum_addr)
        if identity is None or not (0 <= tx.leaf_index < len(identity.keypairs)):
            return False
        msg_hash = tx.signing_hash()
        if not LamportOTS.verify(identity.keypairs[tx.leaf_index]["public_key"], msg_hash, tx.signature):
            return False
        commitment = identity.commitments[tx.leaf_index]
        return MerkleTree.verify(identity.tree.root, commitment, tx.leaf_index, tx.merkle_proof)

    def seal_block(self, prev_hash: bytes, height: int = 1) -> QuantumBlock:
        tx_hashes = [sha256(from_hex(tx.tx_id)) for tx in self._mempool] or [sha256(b"")]
        tree = MerkleTree(tx_hashes)
        block = QuantumBlock(
            height=height,
            prev_hash=prev_hash,
            tx_merkle_root=tree.root,
            timestamp_unix=int(time.time()),
            transactions=list(self._mempool),
        )
        self._mempool.clear()
        return block


# ---------------------------------------------------------------------------
# Seeded demo (mirrors quantum_body_v2.cpp / .json for cross-language parity)
# ---------------------------------------------------------------------------
def run_demo() -> dict:
    engine = QuantumEngine(genesis_seed=2025072607)
    targets = [
        (Chain.PI_NETWORK, "GBPI...QNTM01", 1_000_000),
        (Chain.STELLAR_SOROBAN, "CSOR...QNTM02", 250_000),
        (Chain.ETHEREUM, "0xA1b2C3d4E5F6a7B8c9D0e1F2A3b4C5d6E7f8A9b0", 5_000_000),
        (Chain.SOLANA, "9WzDXwBbmkg8ZTbNMqUxvQRAyrZzDsGYdLVL9zYtAWWM", 750_000),
    ]

    results = []
    for chain, to_addr, amount in targets:
        addr = engine.create_identity(chain, pool_size=8)
        tx = engine.send(addr, chain, to_addr, amount)
        ok = engine.verify_transaction(tx)
        results.append({"quantum_address": addr, "quantum_signature_valid": ok, **tx.to_json()})

    block = engine.seal_block(prev_hash=b"\x00" * 32, height=1)
    return {
        "engine": "pi-nexus-quantum-body-v2 (python)",
        "transactions": results,
        "block": {
            "height": block.height,
            "tx_count": len(block.transactions),
            "merkle_root": to_hex(block.tx_merkle_root),
            "block_hash": to_hex(block.hash()),
        },
    }


if __name__ == "__main__":
    print(json.dumps(run_demo(), indent=2))
