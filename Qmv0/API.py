"""
api_blockchain.py
Pi-Nexus Quantum Body v2 -- REST API

Exposes the quantum-resistant multi-chain transaction engine
(quantum_body_v2.py) over HTTP with FastAPI.

Run:
    pip install fastapi uvicorn --break-system-packages
    uvicorn api_blockchain:app --reload --port 8000

Endpoints:
    GET  /                          service info
    GET  /chains                    supported chains + config
    POST /identity                  create a quantum identity (Merkle-OTS pool) on a chain
    POST /tx/send                   sign + broadcast a quantum-safe transaction
    POST /tx/verify                 verify a transaction's Lamport signature + Merkle proof
    GET  /tx/{tx_id}                fetch a previously sent transaction
    POST /block/seal                seal the current mempool into a QuantumBlock
    GET  /block/{height}            fetch a sealed block
    GET  /demo                      run the seeded cross-language demo dataset
"""

from __future__ import annotations

from typing import Dict, List, Optional

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

from quantum_body_v2 import (
    Chain,
    QuantumEngine,
    QuantumBlock,
    to_hex,
    from_hex,
    run_demo,
)

app = FastAPI(
    title="Pi-Nexus Quantum Body v2 API",
    description="Quantum-resistant (Lamport-OTS + Merkle) transaction API across Ethereum, Solana, Pi Network, and Stellar/Soroban.",
    version="2.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# One shared engine instance for the process; swap for a persisted/DB-backed
# store in production (identities + mempool + sealed blocks are all in-memory here).
engine = QuantumEngine(genesis_seed=2025072607)
_tx_index: Dict[str, dict] = {}
_blocks: Dict[int, QuantumBlock] = {}
_next_height = 1


# ---------------------------------------------------------------------------
# Request / response models
# ---------------------------------------------------------------------------
class CreateIdentityRequest(BaseModel):
    chain: Chain
    pool_size: int = Field(default=16, ge=1, le=256, description="number of one-time-signature keypairs to pre-generate")


class CreateIdentityResponse(BaseModel):
    quantum_address: str
    chain: Chain
    pool_size: int


class SendTxRequest(BaseModel):
    from_quantum_addr: str
    chain: Chain
    to_address: str
    amount_micro: int = Field(ge=0)


class VerifyTxRequest(BaseModel):
    tx_id: str


class VerifyTxResponse(BaseModel):
    tx_id: str
    quantum_signature_valid: bool


class SealBlockRequest(BaseModel):
    prev_hash_hex: Optional[str] = Field(default=None, description="hex of previous block hash; defaults to 32 zero bytes for genesis")


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------
@app.get("/")
def root():
    return {
        "service": "pi-nexus-quantum-body-v2",
        "status": "ok",
        "supported_chains": [c.value for c in Chain],
        "signature_scheme": "lamport-ots-sha256",
    }


@app.get("/chains")
def chains():
    return {
        "chains": [
            {"id": Chain.ETHEREUM.value, "unit": "wei-gwei-scaled", "explorer": "https://etherscan.io/tx/"},
            {"id": Chain.SOLANA.value, "unit": "lamports", "explorer": "https://explorer.solana.com/tx/"},
            {"id": Chain.PI_NETWORK.value, "unit": "pi-stroops", "explorer": "https://explorepi.app/tx/"},
            {"id": Chain.STELLAR_SOROBAN.value, "unit": "stroops", "explorer": "https://stellar.expert/explorer/public/tx/"},
        ]
    }


@app.post("/identity", response_model=CreateIdentityResponse)
def create_identity(req: CreateIdentityRequest):
    addr = engine.create_identity(req.chain, pool_size=req.pool_size)
    return CreateIdentityResponse(quantum_address=addr, chain=req.chain, pool_size=req.pool_size)


@app.post("/tx/send")
def send_tx(req: SendTxRequest):
    try:
        tx = engine.send(req.from_quantum_addr, req.chain, req.to_address, req.amount_micro)
    except ValueError as e:
        raise HTTPException(status_code=404, detail=str(e))
    except RuntimeError as e:
        raise HTTPException(status_code=409, detail=str(e))

    payload = tx.to_json()
    payload["quantum_signature_valid"] = engine.verify_transaction(tx)
    payload["explorer_url"] = engine._adapters[tx.chain].explorer_url(tx)
    _tx_index[tx.tx_id] = payload
    return payload


@app.get("/tx/{tx_id}")
def get_tx(tx_id: str):
    tx = _tx_index.get(tx_id)
    if tx is None:
        raise HTTPException(status_code=404, detail="transaction not found")
    return tx


@app.post("/tx/verify", response_model=VerifyTxResponse)
def verify_tx(req: VerifyTxRequest):
    record = _tx_index.get(req.tx_id)
    if record is None:
        raise HTTPException(status_code=404, detail="transaction not found")
    return VerifyTxResponse(tx_id=req.tx_id, quantum_signature_valid=record["quantum_signature_valid"])


@app.post("/block/seal")
def seal_block(req: SealBlockRequest):
    global _next_height
    prev_hash = from_hex(req.prev_hash_hex) if req.prev_hash_hex else b"\x00" * 32
    block = engine.seal_block(prev_hash=prev_hash, height=_next_height)
    _blocks[_next_height] = block
    result = {
        "height": block.height,
        "prev_hash": to_hex(block.prev_hash),
        "tx_merkle_root": to_hex(block.tx_merkle_root),
        "block_hash": to_hex(block.hash()),
        "tx_count": len(block.transactions),
        "timestamp_unix": block.timestamp_unix,
    }
    _next_height += 1
    return result


@app.get("/block/{height}")
def get_block(height: int):
    block = _blocks.get(height)
    if block is None:
        raise HTTPException(status_code=404, detail="block not found")
    return {
        "height": block.height,
        "prev_hash": to_hex(block.prev_hash),
        "tx_merkle_root": to_hex(block.tx_merkle_root),
        "block_hash": to_hex(block.hash()),
        "transactions": [tx.to_json() for tx in block.transactions],
    }


@app.get("/demo")
def demo():
    return run_demo()
