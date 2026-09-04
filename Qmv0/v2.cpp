// quantum_body_v2.cpp
// Implementation of the Pi-Nexus Quantum Body v2 engine.
// Self-contained: includes a minimal pure-C++ SHA-256 so this compiles
// with zero external dependencies (g++ -std=c++17 quantum_body_v2.cpp -o quantum_body).

#include "quantum_body_v2.hpp"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <random>

namespace pinexus::quantum {

// =======================================================================
// Minimal SHA-256 (public-domain style implementation, self-contained)
// =======================================================================
namespace detail {

struct Sha256Ctx {
    uint32_t h[8];
    uint64_t bitlen = 0;
    uint8_t block[64];
    size_t block_len = 0;
};

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

static void sha256_transform(Sha256Ctx& ctx, const uint8_t* data) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (data[i*4]<<24)|(data[i*4+1]<<16)|(data[i*4+2]<<8)|(data[i*4+3]);
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15]>>3);
        uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2]>>10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=ctx.h[0],b=ctx.h[1],c=ctx.h[2],d=ctx.h[3],e=ctx.h[4],f=ctx.h[5],g=ctx.h[6],h=ctx.h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    ctx.h[0]+=a; ctx.h[1]+=b; ctx.h[2]+=c; ctx.h[3]+=d;
    ctx.h[4]+=e; ctx.h[5]+=f; ctx.h[6]+=g; ctx.h[7]+=h;
}

static void sha256_init(Sha256Ctx& ctx) {
    ctx.h[0]=0x6a09e667; ctx.h[1]=0xbb67ae85; ctx.h[2]=0x3c6ef372; ctx.h[3]=0xa54ff53a;
    ctx.h[4]=0x510e527f; ctx.h[5]=0x9b05688c; ctx.h[6]=0x1f83d9ab; ctx.h[7]=0x5be0cd19;
    ctx.bitlen = 0; ctx.block_len = 0;
}

static void sha256_update(Sha256Ctx& ctx, const uint8_t* data, size_t len) {
    ctx.bitlen += static_cast<uint64_t>(len) * 8;
    size_t i = 0;
    if (ctx.block_len > 0) {
        size_t take = std::min(len, size_t(64) - ctx.block_len);
        std::memcpy(ctx.block + ctx.block_len, data, take);
        ctx.block_len += take;
        i += take;
        if (ctx.block_len == 64) { sha256_transform(ctx, ctx.block); ctx.block_len = 0; }
    }
    for (; i + 64 <= len; i += 64) sha256_transform(ctx, data + i);
    if (i < len) { std::memcpy(ctx.block, data + i, len - i); ctx.block_len = len - i; }
}

static void sha256_final(Sha256Ctx& ctx, uint8_t out[32]) {
    uint8_t pad[72] = {0x80};
    size_t padlen = (ctx.block_len < 56) ? (56 - ctx.block_len) : (120 - ctx.block_len);
    uint64_t bitlen_be = ctx.bitlen;
    sha256_update(ctx, pad, padlen);
    uint8_t lenbytes[8];
    for (int i = 0; i < 8; ++i) lenbytes[i] = (uint8_t)(bitlen_be >> (56 - 8*i));
    // append length directly (bypass counting update)
    size_t take = 8;
    std::memcpy(ctx.block + ctx.block_len, lenbytes, take);
    ctx.block_len += take;
    sha256_transform(ctx, ctx.block);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j)
            out[i*4+j] = (uint8_t)(ctx.h[i] >> (24 - 8*j));
}

} // namespace detail

Bytes32 sha256(const uint8_t* data, size_t len) {
    detail::Sha256Ctx ctx;
    detail::sha256_init(ctx);
    detail::sha256_update(ctx, data, len);
    Bytes32 out{};
    detail::sha256_final(ctx, out.data());
    return out;
}

Bytes32 sha256(const std::vector<uint8_t>& data) { return sha256(data.data(), data.size()); }

Bytes32 sha256_concat(const Bytes32& a, const Bytes32& b) {
    std::vector<uint8_t> buf(a.begin(), a.end());
    buf.insert(buf.end(), b.begin(), b.end());
    return sha256(buf);
}

std::string to_hex(const Bytes32& b) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (auto byte : b) os << std::setw(2) << (int)byte;
    return os.str();
}

std::string to_hex(const std::vector<uint8_t>& b) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (auto byte : b) os << std::setw(2) << (int)byte;
    return os.str();
}

std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back((uint8_t)std::stoul(hex.substr(i, 2), nullptr, 16));
    return out;
}

// =======================================================================
// Lamport OTS
// =======================================================================
static Bytes32 derive(const Bytes32& seed, uint64_t counter) {
    std::vector<uint8_t> buf(seed.begin(), seed.end());
    for (int i = 0; i < 8; ++i) buf.push_back((uint8_t)(counter >> (56 - 8*i)));
    return sha256(buf);
}

LamportKeyPair LamportOTS::keygen(const Bytes32& seed) {
    LamportKeyPair kp;
    uint64_t counter = 0;
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 2; ++j) {
            kp.secret_key[i][j] = derive(seed, counter++);
            kp.public_key[i][j] = sha256(kp.secret_key[i][j].data(), 32);
        }
    }
    return kp;
}

LamportSignature LamportOTS::sign(const LamportKeyPair& kp, const Bytes32& message_hash) {
    LamportSignature sig;
    for (int i = 0; i < 256; ++i) {
        int bit = (message_hash[i / 8] >> (7 - (i % 8))) & 1;
        sig.reveals[i] = kp.secret_key[i][bit];
    }
    return sig;
}

bool LamportOTS::verify(const std::array<std::array<Bytes32, 2>, 256>& public_key,
                         const Bytes32& message_hash,
                         const LamportSignature& sig) {
    for (int i = 0; i < 256; ++i) {
        int bit = (message_hash[i / 8] >> (7 - (i % 8))) & 1;
        Bytes32 h = sha256(sig.reveals[i].data(), 32);
        if (h != public_key[i][bit]) return false;
    }
    return true;
}

Bytes32 LamportOTS::public_key_commitment(const std::array<std::array<Bytes32, 2>, 256>& pk) {
    std::vector<uint8_t> buf;
    buf.reserve(256 * 2 * 32);
    for (auto& pair : pk)
        for (auto& h : pair)
            buf.insert(buf.end(), h.begin(), h.end());
    return sha256(buf);
}

// =======================================================================
// Merkle tree
// =======================================================================
MerkleTree::MerkleTree(std::vector<Bytes32> leaves) {
    if (leaves.empty()) throw std::invalid_argument("MerkleTree requires at least one leaf");
    levels_.push_back(leaves);
    while (levels_.back().size() > 1) {
        const auto& cur = levels_.back();
        std::vector<Bytes32> next;
        for (size_t i = 0; i < cur.size(); i += 2) {
            if (i + 1 < cur.size()) next.push_back(sha256_concat(cur[i], cur[i+1]));
            else next.push_back(sha256_concat(cur[i], cur[i])); // duplicate odd leaf
        }
        levels_.push_back(next);
    }
    root_ = levels_.back()[0];
}

std::vector<Bytes32> MerkleTree::proof(size_t leaf_index) const {
    std::vector<Bytes32> path;
    size_t idx = leaf_index;
    for (size_t lvl = 0; lvl + 1 < levels_.size(); ++lvl) {
        const auto& level = levels_[lvl];
        size_t sibling = (idx % 2 == 0) ? idx + 1 : idx - 1;
        if (sibling >= level.size()) sibling = idx; // duplicated odd node
        path.push_back(level[sibling]);
        idx /= 2;
    }
    return path;
}

bool MerkleTree::verify(const Bytes32& root, const Bytes32& leaf,
                         size_t index, const std::vector<Bytes32>& proof) {
    Bytes32 cur = leaf;
    size_t idx = index;
    for (const auto& sib : proof) {
        cur = (idx % 2 == 0) ? sha256_concat(cur, sib) : sha256_concat(sib, cur);
        idx /= 2;
    }
    return cur == root;
}

// =======================================================================
// Chain adapters (stub broadcast layer -- wire up real RPC clients here)
// =======================================================================
std::string chain_name(Chain c) {
    switch (c) {
        case Chain::Ethereum: return "ethereum";
        case Chain::Solana: return "solana";
        case Chain::PiNetwork: return "pi-network";
        case Chain::StellarSoroban: return "stellar-soroban";
    }
    return "unknown";
}

namespace {
class GenericAdapter : public ChainAdapter {
public:
    explicit GenericAdapter(Chain c) : chain_(c) {}
    Chain chain() const override { return chain_; }
    bool broadcast(const QuantumTransaction& tx) override {
        (void)tx;
        return true; // demo stub: real impl posts to eth_sendRawTransaction /
                      // Solana sendTransaction / Horizon-Soroban submitTransaction
    }
    std::string explorer_url(const QuantumTransaction& tx) const override {
        switch (chain_) {
            case Chain::Ethereum: return "https://etherscan.io/tx/0x" + tx.tx_id;
            case Chain::Solana: return "https://explorer.solana.com/tx/" + tx.tx_id;
            case Chain::PiNetwork: return "https://explorepi.app/tx/" + tx.tx_id;
            case Chain::StellarSoroban: return "https://stellar.expert/explorer/public/tx/" + tx.tx_id;
        }
        return "";
    }
private:
    Chain chain_;
};
} // namespace

std::unique_ptr<ChainAdapter> make_adapter(Chain c) {
    return std::make_unique<GenericAdapter>(c);
}

// =======================================================================
// QuantumTransaction / QuantumBlock hashing
// =======================================================================
Bytes32 QuantumTransaction::signing_hash() const {
    std::ostringstream os;
    os << from_quantum_addr << "|" << to_address << "|" << amount_micro
       << "|" << nonce << "|" << timestamp_unix << "|" << chain_name(chain);
    std::string s = os.str();
    return sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

Bytes32 QuantumBlock::hash() const {
    std::ostringstream os;
    os << height << "|" << to_hex(prev_hash) << "|" << to_hex(tx_merkle_root) << "|" << timestamp_unix;
    std::string s = os.str();
    return sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// =======================================================================
// QuantumEngine
// =======================================================================
QuantumEngine::QuantumEngine(uint64_t genesis_seed) : seed_counter_(genesis_seed) {
    for (Chain c : {Chain::Ethereum, Chain::Solana, Chain::PiNetwork, Chain::StellarSoroban})
        adapters_[(int)c] = make_adapter(c);
}

Bytes32 QuantumEngine::next_seed() {
    Bytes32 base{};
    for (int i = 0; i < 8; ++i) base[i] = (uint8_t)(seed_counter_ >> (56 - 8*i));
    seed_counter_++;
    return sha256(base.data(), 32);
}

std::string QuantumEngine::create_identity(Chain c, size_t pool_size) {
    Identity id{c, {}, {}, MerkleTree({sha256(std::vector<uint8_t>{0})})}; // placeholder tree, replaced below
    id.keypairs.reserve(pool_size);
    id.commitments.reserve(pool_size);
    for (size_t i = 0; i < pool_size; ++i) {
        auto kp = LamportOTS::keygen(next_seed());
        auto commitment = LamportOTS::public_key_commitment(kp.public_key);
        id.keypairs.push_back(kp);
        id.commitments.push_back(commitment);
    }
    MerkleTree tree(id.commitments);
    std::string root_hex = to_hex(tree.root());
    id.tree = tree;
    identities_.emplace(root_hex, std::move(id));
    return root_hex;
}

QuantumTransaction QuantumEngine::send(const std::string& from_addr_hex, Chain c,
                                        const std::string& to_address, uint64_t amount_micro) {
    auto it = identities_.find(from_addr_hex);
    if (it == identities_.end()) throw std::runtime_error("unknown quantum identity: " + from_addr_hex);
    Identity& id = it->second;
    if (id.next_leaf >= id.keypairs.size())
        throw std::runtime_error("OTS pool exhausted for identity " + from_addr_hex + " -- rotate identity");

    QuantumTransaction tx;
    if (id.chain != c)
        throw std::invalid_argument("identity belongs to a different chain");
    tx.chain = c;
    tx.from_quantum_addr = from_addr_hex;
    tx.to_address = to_address;
    tx.amount_micro = amount_micro;
    tx.nonce = id.nonce++;
    tx.timestamp_unix = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    Bytes32 msg_hash = tx.signing_hash();
    size_t leaf = id.next_leaf++;
    tx.leaf_index = leaf;
    tx.signature = LamportOTS::sign(id.keypairs[leaf], msg_hash);
    tx.merkle_proof = id.tree.proof(leaf);
    tx.tx_id = to_hex(msg_hash);

    if (!adapters_[(int)c]->broadcast(tx))
        throw std::runtime_error("chain adapter rejected transaction");
    mempool_.push_back(tx);
    return tx;
}

bool QuantumEngine::verify_transaction(const QuantumTransaction& tx) const {
    auto it = identities_.find(tx.from_quantum_addr);
    if (it == identities_.end()) return false;
    const Identity& id = it->second;
    if (tx.leaf_index >= id.keypairs.size()) return false;

    Bytes32 msg_hash = tx.signing_hash();
    if (!LamportOTS::verify(id.keypairs[tx.leaf_index].public_key, msg_hash, tx.signature))
        return false;

    Bytes32 commitment = id.commitments[tx.leaf_index];
    return MerkleTree::verify(id.tree.root(), commitment, tx.leaf_index, tx.merkle_proof);
}

QuantumBlock QuantumEngine::seal_block(const Bytes32& prev_hash) {
    QuantumBlock block;
    block.height = 0; // caller sets real height in a full chain context
    block.prev_hash = prev_hash;
    block.timestamp_unix = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    block.transactions = mempool_;

    std::vector<Bytes32> tx_hashes;
    tx_hashes.reserve(mempool_.size());
    for (auto& tx : mempool_) tx_hashes.push_back(sha256(from_hex(tx.tx_id)));
    if (tx_hashes.empty()) tx_hashes.push_back(sha256(std::vector<uint8_t>{}));
    MerkleTree tree(tx_hashes);
    block.tx_merkle_root = tree.root();

    mempool_.clear();
    return block;
}

} // namespace pinexus::quantum

// ===========================================================================
// Demo entrypoint -- builds identities on all four chains, sends a tx on
// each, verifies signatures, and seals a block. Mirrors the seeded demo
// data used in quantum_body_v2.json / quantum_body_v2.py for cross-language
// parity checks.
// ===========================================================================
#ifndef QUANTUM_BODY_NO_MAIN
int main() {
    using namespace pinexus::quantum;

    std::cout << "=== Pi-Nexus Quantum Body v2 (C++17 reference engine) ===\n";
    QuantumEngine engine(/*genesis_seed=*/2025072607); // seeded for reproducibility

    struct Target { Chain chain; std::string to; uint64_t amount; };
    std::vector<Target> targets = {
        {Chain::PiNetwork,       "GBPI...QNTM01", 1'000'000},
        {Chain::StellarSoroban,  "CSOR...QNTM02", 250'000},
        {Chain::Ethereum,        "0xA1b2C3d4E5F6a7B8c9D0e1F2A3b4C5d6E7f8A9b0", 5'000'000},
        {Chain::Solana,          "9WzDXwBbmkg8ZTbNMqUxvQRAyrZzDsGYdLVL9zYtAWWM", 750'000},
    };

    for (auto& t : targets) {
        std::string addr = engine.create_identity(t.chain, /*pool_size=*/8);
        std::cout << chain_name(t.chain) << " quantum identity: " << addr << "\n";
        QuantumTransaction tx = engine.send(addr, t.chain, t.to, t.amount);
        bool ok = engine.verify_transaction(tx);
        std::cout << "  tx " << tx.tx_id.substr(0, 16) << "...  amount=" << tx.amount_micro
                  << "  quantum_signature_valid=" << (ok ? "true" : "false") << "\n";
    }

    Bytes32 genesis_prev{};
    QuantumBlock block = engine.seal_block(genesis_prev);
    block.height = 1;
    std::cout << "\nSealed block #" << block.height
              << " txs=" << block.transactions.size()
              << " merkle_root=" << to_hex(block.tx_merkle_root)
              << " block_hash=" << to_hex(block.hash()) << "\n";
    return 0;
}
#endif
