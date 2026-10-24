// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/UploadOrder.h"

#include "core/AtomicRef.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <bit>

namespace sf {

namespace {

// Mesh vertex -> local index within one cluster: an open-addressing table, cleared by bumping a stamp. Big
// enough for a cluster's worst case (3 distinct vertices per triangle) at under half load.
struct LocalMap {
    static constexpr uint32_t kBits = 15, kSize = 1u << kBits;
    std::vector<uint32_t> keys = std::vector<uint32_t>(kSize), stamps = std::vector<uint32_t>(kSize, 0);
    std::vector<uint16_t> values = std::vector<uint16_t>(kSize);
    uint32_t stamp = 0;
    uint32_t count = 0;
    void clear() {
        if (++stamp == 0) {
            std::fill(stamps.begin(), stamps.end(), 0);
            stamp = 1;
        }
        count = 0;
    }
    // The local index of v, assigning the next one on first use (`isNew` says which).
    uint16_t get(uint32_t v, bool& isNew) {
        for (uint32_t h = (v * 0x9E3779B1u) >> (32 - kBits);; h = (h + 1) & (kSize - 1)) {
            if (stamps[h] != stamp) {
                stamps[h] = stamp;
                keys[h] = v;
                values[h] = uint16_t(count++);
                isNew = true;
                return values[h];
            }
            if (keys[h] == v) {
                isNew = false;
                return values[h];
            }
        }
    }
};

LocalMap& localMap() {
    thread_local LocalMap m;
    return m;
}

// Calls fn(local, meshVertex, isNew) for every corner of cluster c's triangles in order.
template <class F>
void walkCluster(const Mesh& mesh, const UploadOrder& order, uint32_t c, F&& fn) {
    LocalMap& map = localMap();
    map.clear();
    const uint32_t nv = uint32_t(mesh.vertexCount());
    const uint32_t n = order.clusterTriangles(c);
    for (uint32_t i = 0; i < n; ++i) {
        const glm::uvec3& tri = mesh.triangles[order.triangle(c, i)];
        const bool valid = tri.x < nv && tri.y < nv && tri.z < nv;
        for (int k = 0; k < 3; ++k) {
            if (!valid) {
                fn(-1, 0u, false);
                continue;
            }
            bool isNew = false;
            const uint16_t local = map.get(tri[k], isNew);
            fn(int(local), tri[k], isNew);
        }
    }
}

// A 64-bit mix (splitmix64's finaliser) for comparing neighbour multisets as sums.
uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Sorts (key, value) pairs by key, keeping equal keys in order (LSD radix, 11 bits per pass).
template <class V>
void radixSortByKey(std::vector<std::pair<uint32_t, V>>& items, uint32_t maxKey) {
    if (items.size() < 2) return;
    std::vector<std::pair<uint32_t, V>> tmp(items.size());
    constexpr uint32_t kDigit = 11, kBuckets = 1u << kDigit;
    const int bits = std::max(1, int(std::bit_width(maxKey)));
    std::vector<uint32_t> count(kBuckets);
    for (int shift = 0; shift < bits; shift += int(kDigit)) {
        std::fill(count.begin(), count.end(), 0);
        for (const auto& e : items) ++count[(e.first >> shift) & (kBuckets - 1)];
        uint32_t sum = 0;
        for (uint32_t& n : count) sum += std::exchange(n, sum);
        for (const auto& e : items) tmp[count[(e.first >> shift) & (kBuckets - 1)]++] = e;
        items.swap(tmp);
    }
}

// Spreads the low 10 bits of x to every third bit (Morton codes).
uint32_t spread3(uint32_t x) {
    x &= 0x3FF;
    x = (x | (x << 16)) & 0x030000FF;
    x = (x | (x << 8)) & 0x0300F00F;
    x = (x | (x << 4)) & 0x030C30C3;
    x = (x | (x << 2)) & 0x09249249;
    return x;
}

} // namespace

uint32_t UploadOrder::clusterOfSlot(uint32_t i) const {
    const auto it = std::upper_bound(clusters_.begin(), clusters_.end(), i,
                                     [](uint32_t s, const Cluster& c) { return s < c.firstVertex; });
    return it == clusters_.begin() ? 0u : uint32_t(it - clusters_.begin() - 1);
}

void UploadOrder::build(const Mesh& mesh, const ClusterCallback& onCluster) {
    triangles_ = mesh.triangleCount();
    vertices_ = mesh.vertexCount();
    // Chunks along a Morton curve of their centres (10 bits per axis over the mesh's bounds); a partial last
    // chunk goes last, so every cluster but the last is made of whole chunks.
    const size_t fullChunks = triangles_ >> kChunkShift, nChunks = (triangles_ + kChunkTriangles - 1) >> kChunkShift;
    std::vector<AABB> boxes(fullChunks);
    poolFor(fullChunks, [&](size_t k0, size_t k1) {
        for (size_t k = k0; k < k1; ++k) {
            AABB box;
            for (size_t t = k << kChunkShift; t < (k + 1) << kChunkShift; ++t)
                for (int c = 0; c < 3; ++c)
                    if (mesh.triangles[t][c] < vertices_) box.expand(mesh.positions[mesh.triangles[t][c]]);
            boxes[k] = box;
        }
    }, 256);
    AABB all;
    for (const AABB& b : boxes)
        if (b.valid()) all.expand(b);
    std::vector<std::pair<uint32_t, uint32_t>> keyed(fullChunks);
    const glm::vec3 lo = all.valid() ? all.min : glm::vec3(0.0f);
    const glm::vec3 ext = all.valid() ? glm::max(all.extent(), glm::vec3(1e-30f)) : glm::vec3(1.0f);
    poolFor(fullChunks, [&](size_t k0, size_t k1) {
        for (size_t k = k0; k < k1; ++k) {
            uint32_t code = 0;
            if (boxes[k].valid()) {
                const glm::vec3 q = glm::clamp((boxes[k].center() - lo) / ext, 0.0f, 1.0f) * 1023.0f;
                code = spread3(uint32_t(q.x)) | (spread3(uint32_t(q.y)) << 1) | (spread3(uint32_t(q.z)) << 2);
            }
            keyed[k] = {code, uint32_t(k)};
        }
    }, 4096);
    std::vector<AABB>().swap(boxes);
    radixSortByKey(keyed, (1u << 30) - 1);
    chunks_.resize(nChunks);
    for (size_t k = 0; k < fullChunks; ++k) chunks_[k] = keyed[k].second;
    if (nChunks > fullChunks) chunks_.back() = uint32_t(fullChunks);
    const size_t nc = (nChunks + kClusterChunks - 1) / kClusterChunks;
    clusters_.assign(nc, Cluster{});
    for (size_t c = 0, first = 0; c < nc; ++c) {
        const size_t endChunk = std::min(nChunks, (c + 1) * kClusterChunks);
        const size_t count = std::min(triangles_, endChunk << kChunkShift) - (c * kClusterChunks << kChunkShift);
        clusters_[c].firstTriangle = uint32_t(first);
        clusters_[c].triangleCount = uint32_t(count);
        first += count;
    }
    // Distinct vertices per cluster, then block-aligned ranges.
    poolFor(nc, [&](size_t c0, size_t c1) {
        for (size_t c = c0; c < c1; ++c) {
            uint32_t n = 0;
            walkCluster(mesh, *this, uint32_t(c), [&](int, uint32_t, bool isNew) { n += isNew; });
            clusters_[c].vertexCount = n;
        }
    }, 1);
    size_t slots = 0;
    for (Cluster& c : clusters_) {
        c.firstVertex = uint32_t(slots);
        slots += (size_t(c.vertexCount) + kBlock - 1) & ~size_t(kBlock - 1);
    }
    // (Filled in parallel: these are tens of millions of entries.)
    fromGpu_.resize(slots);
    toGpu_.resize(vertices_);
    poolFor(vertices_, [&](size_t b, size_t e) { std::fill(toGpu_.begin() + std::ptrdiff_t(b), toGpu_.begin() + std::ptrdiff_t(e), kNone); },
            1 << 16);
    std::vector<std::vector<std::pair<uint32_t, uint64_t>>> unbalanced(nc);
    // Each cluster's vertices in first-use order; a vertex's first slot is the lowest (an atomic min, so the
    // result doesn't depend on which thread gets there first).
    poolFor(nc, [&](size_t c0, size_t c1) {
        for (size_t c = c0; c < c1; ++c) {
            const Cluster& cl = clusters_[c];
            thread_local std::vector<uint16_t> locals;
            thread_local std::vector<uint64_t> balance; // per local vertex: sum of hash(next) - hash(previous)
            locals.clear();
            balance.assign(cl.vertexCount, 0);
            uint32_t corner[3], cornerLocal[3];
            int k = 0;
            walkCluster(mesh, *this, uint32_t(c), [&](int local, uint32_t v, bool isNew) {
                if (onCluster) locals.push_back(uint16_t(std::max(local, 0)));
                if (local >= 0) {
                    corner[k] = v;
                    cornerLocal[k] = uint32_t(local);
                }
                if (++k == 3) {
                    k = 0;
                    if (local >= 0)
                        for (int j = 0; j < 3; ++j)
                            balance[cornerLocal[j]] += mix64(corner[(j + 1) % 3]) - mix64(corner[(j + 2) % 3]);
                }
                if (!isNew) return;
                const uint32_t slot = cl.firstVertex + uint32_t(local);
                fromGpu_[slot] = v;
                AtomicRef<uint32_t> first(toGpu_[v]);
                uint32_t cur = first.load(std::memory_order_relaxed);
                while (slot < cur && !first.compare_exchange_weak(cur, slot, std::memory_order_relaxed)) {
                }
            });
            if (onCluster) onCluster(uint32_t(c), locals.data());
            // Vertices whose edges don't balance within this cluster: settled once every cluster has added in.
            for (uint32_t l = 0; l < cl.vertexCount; ++l)
                if (balance[l] != 0) unbalanced[c].emplace_back(fromGpu_[cl.firstVertex + l], balance[l]);
            const uint32_t padEnd = c + 1 < nc ? clusters_[c + 1].firstVertex : uint32_t(slots);
            const uint32_t fill = cl.vertexCount ? fromGpu_[cl.firstVertex + cl.vertexCount - 1] : 0u;
            std::fill(fromGpu_.begin() + cl.firstVertex + cl.vertexCount, fromGpu_.begin() + padEnd, fill);
        }
    }, 1);
    // The other copies, by vertex (a stable sort of the slot-ordered list, so slots stay increasing).
    copies_.clear();
    hasCopy_.assign((vertices_ + 63) / 64, 0);
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> perCluster(nc);
    poolFor(nc, [&](size_t c0, size_t c1) {
        for (size_t c = c0; c < c1; ++c) {
            const Cluster& cl = clusters_[c];
            for (uint32_t i = cl.firstVertex; i < cl.firstVertex + cl.vertexCount; ++i)
                if (toGpu_[fromGpu_[i]] != i) perCluster[c].emplace_back(fromGpu_[i], i);
        }
    }, 16);
    size_t copyCount = 0;
    for (const auto& p : perCluster) copyCount += p.size();
    std::vector<std::pair<uint32_t, uint32_t>> bySlot;
    bySlot.reserve(copyCount);
    for (auto& p : perCluster) {
        bySlot.insert(bySlot.end(), p.begin(), p.end());
        std::vector<std::pair<uint32_t, uint32_t>>().swap(p);
    }
    radixSortByKey(bySlot, uint32_t(vertices_));
    // Closed: the unbalanced vertices (on seams, or open) sum to zero over all clusters.
    {
        std::vector<std::pair<uint32_t, uint64_t>> all;
        for (auto& u : unbalanced) {
            all.insert(all.end(), u.begin(), u.end());
            std::vector<std::pair<uint32_t, uint64_t>>().swap(u);
        }
        radixSortByKey(all, uint32_t(vertices_));
        closed_ = triangles_ > 0;
        for (size_t i = 0; i < all.size() && closed_;) {
            uint64_t sum = 0;
            size_t j = i;
            for (; j < all.size() && all[j].first == all[i].first; ++j) sum += all[j].second;
            closed_ = sum == 0;
            i = j;
        }
    }
    for (const auto& e : bySlot) hasCopy_[e.first >> 6] |= uint64_t(1) << (e.first & 63);
    copies_ = std::move(bySlot);
    marked_.assign((slots + kBlock - 1) / kBlock, 0);
    blocks_.clear();
}

void UploadOrder::slotsOf(uint32_t v, std::vector<uint32_t>& out) const {
    out.clear();
    if (v >= toGpu_.size() || toGpu_[v] == kNone) return;
    out.push_back(toGpu_[v]);
    if (!((hasCopy_[v >> 6] >> (v & 63)) & 1)) return;
    auto it = std::lower_bound(copies_.begin(), copies_.end(), std::make_pair(v, 0u));
    for (; it != copies_.end() && it->first == v; ++it) out.push_back(it->second);
}

void UploadOrder::localTriangles(const Mesh& mesh, uint32_t c, uint16_t* out) const {
    walkCluster(mesh, *this, c, [&](int local, uint32_t, bool) { *out++ = uint16_t(std::max(local, 0)); });
}

void UploadOrder::dirtyRuns(const Mesh& mesh, std::vector<std::pair<uint32_t, uint32_t>>& out) {
    out.clear();
    const uint32_t nv = uint32_t(std::min(mesh.vertexCount(), vertices_));
    const uint32_t ns = uint32_t(fromGpu_.size());
    const uint32_t end = std::min(mesh.dirtyEnd, nv);
    if (mesh.dirtyBegin >= end || ns == 0) return;
    const size_t firstBlock = mesh.dirtyBegin >> Mesh::kDirtyBlockShift;
    const size_t lastBlock = std::min(mesh.dirtyBlocks.size(), size_t((end - 1) >> Mesh::kDirtyBlockShift) + 1);
    constexpr size_t kWordsPerBlock = Mesh::kDirtyBlock / 64;
    size_t dirty = 0;
    for (size_t b = firstBlock; b < lastBlock; ++b)
        if (mesh.dirtyBlocks[b])
            for (size_t w = b * kWordsPerBlock; w < (b + 1) * kWordsPerBlock && w < mesh.dirtyBits.size(); ++w)
                dirty += size_t(std::popcount(mesh.dirtyBits[w]));
    if (dirty * 4 > nv || mesh.dirtyBits.size() * 64 < nv) {
        out.emplace_back(0, ns); // a good part of the mesh (or no per-vertex marks): everything, in one go
        return;
    }
    auto mark = [&](uint32_t slot) {
        const uint32_t g = slot >> kBlockShift;
        if (!marked_[g]) {
            marked_[g] = 1;
            blocks_.push_back(g);
        }
    };
    // The GPU blocks of every slot of every marked vertex.
    for (size_t b = firstBlock; b < lastBlock; ++b) {
        if (!mesh.dirtyBlocks[b]) continue;
        for (size_t w = b * kWordsPerBlock; w < (b + 1) * kWordsPerBlock && w < mesh.dirtyBits.size(); ++w) {
            const uint64_t copies = w < hasCopy_.size() ? hasCopy_[w] : 0;
            for (uint64_t bits = mesh.dirtyBits[w]; bits; bits &= bits - 1) {
                const int bit = std::countr_zero(bits);
                const size_t v = w * 64 + size_t(bit);
                if (v >= nv) break;
                if (toGpu_[v] == kNone) continue;
                mark(toGpu_[v]);
                if ((copies >> bit) & 1)
                    for (auto it = std::lower_bound(copies_.begin(), copies_.end(), std::make_pair(uint32_t(v), 0u));
                         it != copies_.end() && it->first == v; ++it)
                        mark(it->second);
            }
        }
    }
    std::sort(blocks_.begin(), blocks_.end());
    constexpr uint32_t kJoin = 4 * kBlock; // fewer, slightly longer uploads
    for (uint32_t g : blocks_) {
        marked_[g] = 0;
        const uint32_t lo = g << kBlockShift, hi = std::min(ns, (g + 1) << kBlockShift);
        if (!out.empty() && lo - out.back().second <= kJoin) out.back().second = hi;
        else out.emplace_back(lo, hi);
    }
    blocks_.clear();
}

size_t UploadOrder::memoryBytes() const {
    return (toGpu_.capacity() + fromGpu_.capacity() + blocks_.capacity()) * sizeof(uint32_t) + marked_.capacity() +
           copies_.capacity() * sizeof(copies_[0]) + hasCopy_.capacity() * sizeof(uint64_t) + clusters_.capacity() * sizeof(Cluster) +
           chunks_.capacity() * sizeof(uint32_t);
}

} // namespace sf
