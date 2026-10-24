// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Mesh.h"
#include "core/Multires.h"
#include "core/AtomicRef.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_map>

namespace sf {

static glm::vec3 faceNormal(const std::vector<glm::vec3>& p, const glm::uvec3& t) {
    // Unnormalised: magnitude is twice the triangle area, giving area weighting.
    return glm::cross(p[t.y] - p[t.x], p[t.z] - p[t.x]);
}

bool Mesh::vertexHidden(uint32_t v) const {
    if (hidden.empty()) return false;
    auto tris = vertexTriangles(v);
    if (tris.empty()) return false;
    for (uint32_t t : tris)
        if (!hidden[t]) return false;
    return true;
}

std::vector<float>& Mesh::ensureMask(float fill) {
    if (mask.size() != positions.size()) mask.assign(positions.size(), fill);
    return mask;
}

std::vector<glm::vec3>& Mesh::ensureColors(const glm::vec3& fill) {
    if (colors.size() != positions.size()) colors.assign(positions.size(), fill);
    return colors;
}

std::vector<uint32_t>& Mesh::ensurePolygroups(uint32_t fill) {
    if (polygroups.size() != triangles.size()) polygroups.assign(triangles.size(), fill);
    return polygroups;
}

std::vector<uint8_t>& Mesh::ensureHidden() {
    if (hidden.size() != triangles.size()) hidden.assign(triangles.size(), 0);
    return hidden;
}

std::vector<uint32_t>& Mesh::ensureQuadPartners() {
    if (quadPartners.size() != triangles.size()) quadPartners.assign(triangles.size(), kNoQuad);
    return quadPartners;
}

void Mesh::pairQuad(uint32_t t, uint32_t u) {
    // Grows (rather than resets) the array, so pairs can be recorded while triangles are being added.
    if (quadPartners.size() < triangles.size()) quadPartners.resize(triangles.size(), kNoQuad);
    quadPartners[t] = u;
    quadPartners[u] = t;
}

int Mesh::quadDiagonal(uint32_t t, uint32_t u) const {
    if (t == u || t >= triangles.size() || u >= triangles.size()) return -1;
    const glm::uvec3& a = triangles[t];
    const glm::uvec3& b = triangles[u];
    if (a.x == a.y || a.y == a.z || a.x == a.z || b.x == b.y || b.y == b.z || b.x == b.z) return -1;
    for (int e = 0; e < 3; ++e)
        for (int k = 0; k < 3; ++k)
            if (b[k] == a[(e + 1) % 3] && b[(k + 1) % 3] == a[e]) {
                const uint32_t far = b[(k + 2) % 3];
                return far == a[(e + 2) % 3] ? -1 : e; // the same triangle twice is no quad
            }
    return -1;
}

int Mesh::exportFace(uint32_t t, glm::uvec4& face) const {
    const glm::uvec3& a = triangles[t];
    const uint32_t u = quadPartnerAt(t);
    const int e = u == kNoQuad ? -1 : quadDiagonal(t, u);
    if (e < 0) {
        face = glm::uvec4(a, kNoQuad);
        return 3;
    }
    if (u < t) return 0;
    const glm::uvec3& b = triangles[u];
    uint32_t far = b.x;
    for (int k = 0; k < 3; ++k)
        if (b[k] != a[e] && b[k] != a[(e + 1) % 3]) far = b[k];
    face = glm::uvec4(a[(e + 1) % 3], a[(e + 2) % 3], a[e], far);
    return 4;
}

size_t Mesh::quadCount() const {
    size_t n = 0;
    for (uint32_t t = 0; t < quadPartners.size(); ++t) {
        const uint32_t u = quadPartnerAt(t);
        n += u != kNoQuad && u > t;
    }
    return n;
}

void Mesh::sanitizeAttributes() {
    if (!mask.empty() && mask.size() != positions.size()) clearMask();
    if (!colors.empty() && colors.size() != positions.size()) clearColors();
    if (!polygroups.empty() && polygroups.size() != triangles.size()) clearPolygroups();
    if (!hidden.empty() && hidden.size() != triangles.size()) clearHidden();
    if (!uvs.empty() && uvs.size() != triangles.size() * 3) std::vector<glm::vec2>().swap(uvs);
    if (!quadPartners.empty() && quadPartners.size() != triangles.size()) clearQuads();
    // Quad pairs must be mutual and share an edge: unpair anything an edit or a malformed file left behind.
    bool anyQuad = false;
    for (uint32_t t = 0; t < quadPartners.size(); ++t) {
        const uint32_t u = quadPartners[t];
        if (u == kNoQuad) continue;
        if (u >= quadPartners.size() || quadPartners[u] != t || quadDiagonal(t, u) < 0) quadPartners[t] = kNoQuad;
        else anyQuad = true;
    }
    // A triangle unpaired above may still be named by its former partner, visited earlier.
    for (uint32_t t = 0; t < quadPartners.size(); ++t) {
        const uint32_t u = quadPartners[t];
        if (u != kNoQuad && quadPartners[u] != t) quadPartners[t] = kNoQuad;
    }
    if (!anyQuad) clearQuads();
    sanitizePaintLayers(*this);
    if (multires && !subdivMatches(*this)) multires.reset();
}

void Mesh::rebuildTopology() {
    sanitizeAttributes();
    const size_t nv = positions.size();

    vtOffsets_.assign(nv + 1, 0);
    for (const auto& t : triangles)
        for (int c = 0; c < 3; ++c) ++vtOffsets_[t[c] + 1];
    for (size_t i = 0; i < nv; ++i) vtOffsets_[i + 1] += vtOffsets_[i];
    vtList_.resize(vtOffsets_[nv]);
    {
        std::vector<uint32_t> cursor(vtOffsets_.begin(), vtOffsets_.end() - 1);
        for (uint32_t ti = 0; ti < triangles.size(); ++ti)
            for (int c = 0; c < 3; ++c) vtList_[cursor[triangles[ti][c]]++] = ti;
    }

    // Rings (sorted, unique) per vertex: sized in one parallel pass, written in a second.
    adjOffsets_.assign(nv + 1, 0);
    auto ring = [&](uint32_t v, std::vector<uint32_t>& out) {
        out.clear();
        for (uint32_t ti : vertexTriangles(v))
            for (int c = 0; c < 3; ++c)
                if (triangles[ti][c] != v) out.push_back(triangles[ti][c]);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };
    poolFor(nv, [&](size_t b, size_t e) {
        std::vector<uint32_t> r;
        for (size_t v = b; v < e; ++v) {
            ring(uint32_t(v), r);
            adjOffsets_[v + 1] = uint32_t(r.size());
        }
    });
    for (size_t i = 0; i < nv; ++i) adjOffsets_[i + 1] += adjOffsets_[i];
    std::vector<uint32_t>().swap(adjList_); // exact size, no spare capacity
    adjList_.resize(adjOffsets_[nv]);
    poolFor(nv, [&](size_t b, size_t e) {
        std::vector<uint32_t> r;
        for (size_t v = b; v < e; ++v) {
            ring(uint32_t(v), r);
            std::copy(r.begin(), r.end(), adjList_.begin() + adjOffsets_[v]);
        }
    });

    vertStamp_.assign(nv, 0);
    stamp_ = 0;
    normals.resize(nv);
    dirtyBlocks.assign((nv + kDirtyBlock - 1) / kDirtyBlock, 0);
    dirtyBits.assign(dirtyBlocks.size() * (kDirtyBlock / 64), 0);
    if (dirtyBegin < dirtyEnd) markDirtyRange(dirtyBegin, std::min(dirtyEnd, uint32_t(nv)));
    topologyDirty = true;
    facesDirty = true;
}

uint32_t Mesh::nextStamp() {
    if (++stamp_ == 0) {
        std::fill(vertStamp_.begin(), vertStamp_.end(), 0);
        stamp_ = 1;
    }
    return stamp_;
}

void Mesh::growDirtyBlocks(size_t blocks) {
    if (dirtyBlocks.size() < blocks) dirtyBlocks.resize(blocks, 0);
    if (dirtyBits.size() < blocks * (kDirtyBlock / 64)) dirtyBits.resize(blocks * (kDirtyBlock / 64), 0);
}

void Mesh::markDirtyRange(uint32_t begin, uint32_t end) {
    if (begin >= end) return;
    dirtyBegin = std::min(dirtyBegin, begin);
    dirtyEnd = std::max(dirtyEnd, end);
    const size_t last = (end - 1) >> kDirtyBlockShift;
    growDirtyBlocks(last + 1);
    std::fill(dirtyBlocks.begin() + (begin >> kDirtyBlockShift), dirtyBlocks.begin() + last + 1, uint8_t(1));
    // Bits: the partial words at both ends, whole words between.
    const size_t w0 = begin >> 6, w1 = (end - 1) >> 6;
    const uint64_t head = ~uint64_t(0) << (begin & 63), tail = ~uint64_t(0) >> (63 - ((end - 1) & 63));
    if (w0 == w1) {
        dirtyBits[w0] |= head & tail;
        return;
    }
    dirtyBits[w0] |= head;
    std::fill(dirtyBits.begin() + w0 + 1, dirtyBits.begin() + w1, ~uint64_t(0));
    dirtyBits[w1] |= tail;
}

void Mesh::clearDirty() {
    if (dirtyBegin < dirtyEnd) {
        constexpr size_t kWords = kDirtyBlock / 64;
        const size_t b = dirtyBegin >> kDirtyBlockShift, e = std::min(dirtyBlocks.size(), size_t((dirtyEnd - 1) >> kDirtyBlockShift) + 1);
        for (size_t i = b; i < e; ++i) {
            if (!dirtyBlocks[i]) continue;
            dirtyBlocks[i] = 0;
            std::fill(dirtyBits.begin() + std::ptrdiff_t(i * kWords), dirtyBits.begin() + std::ptrdiff_t((i + 1) * kWords), 0);
        }
    }
    dirtyBegin = std::numeric_limits<uint32_t>::max();
    dirtyEnd = 0;
    topologyDirty = false;
    facesDirty = false;
}

void Mesh::recomputeAllNormals() {
    // Area-weighted face normals summed in triangle order (each vertex's sum in the same order as
    // updateNormals adds them); the faces and the normalising run in parallel.
    std::vector<glm::vec3> faces(triangles.size());
    poolFor(triangles.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) faces[i] = faceNormal(positions, triangles[i]);
    }, 1 << 16);
    normals.assign(positions.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < triangles.size(); ++i)
        for (int c = 0; c < 3; ++c) normals[triangles[i][c]] += faces[i];
    poolFor(normals.size(), [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            glm::vec3& n = normals[v];
            float len = glm::length(n);
            n = len > 1e-20f ? n / len : glm::vec3(0, 1, 0);
        }
    }, 1 << 16);
    markAllDirty();
}

void Mesh::updateNormals(std::span<const uint32_t> verts) {
    // The vertices whose normal changes: the moved ones and every neighbour (each once), then each one's
    // normal from its faces, in its triangle order (as recomputeAllNormals sums them).
    const size_t nv = positions.size();
    if (vertStamp_.size() != nv) return; // no topology
    growDirtyBlocks((nv + kDirtyBlock - 1) / kDirtyBlock);
    constexpr size_t kBlock = 4096;
    scratchVerts_.clear();
    if (verts.size() <= 4 * kBlock || ThreadPool::shared().serial()) {
        const uint32_t s = nextStamp();
        auto claim = [&](uint32_t u) {
            if (vertStamp_[u] == s) return;
            vertStamp_[u] = s;
            scratchVerts_.push_back(u);
        };
        for (uint32_t v : verts) {
            claim(v);
            for (uint32_t u : neighbors(v)) claim(u);
        }
    } else {
        // In parallel: first the moved vertices themselves (stamp `own`), then the ring around them that isn't
        // one of them (stamp `ring`, claimed with an atomic exchange: only the ring is shared between blocks).
        // Which block lists a ring vertex varies, but the set, and every normal computed from it, doesn't.
        if (stamp_ >= 0xFFFFFFF0u) { // two stamps in a row: start over first, so they can't wrap in between
            std::fill(vertStamp_.begin(), vertStamp_.end(), 0);
            stamp_ = 0;
        }
        const uint32_t own = nextStamp(), ring = nextStamp();
        const size_t parts = (verts.size() + kBlock - 1) / kBlock;
        if (scratchParts_.size() < 2 * parts) scratchParts_.resize(2 * parts);
        poolFor(verts.size(), [&](size_t b, size_t e) {
            std::vector<uint32_t>& out = scratchParts_[b / kBlock];
            out.clear();
            for (size_t i = b; i < e; ++i)
                if (AtomicRef<uint32_t>(vertStamp_[verts[i]]).exchange(own, std::memory_order_relaxed) != own)
                    out.push_back(verts[i]);
        }, kBlock);
        poolFor(verts.size(), [&](size_t b, size_t e) {
            std::vector<uint32_t>& out = scratchParts_[parts + b / kBlock];
            out.clear();
            for (size_t i = b; i < e; ++i)
                for (uint32_t u : neighbors(verts[i])) {
                    AtomicRef<uint32_t> stamp(vertStamp_[u]);
                    const uint32_t seen = stamp.load(std::memory_order_relaxed);
                    if (seen == own || seen == ring) continue;
                    if (stamp.exchange(ring, std::memory_order_relaxed) != ring) out.push_back(u);
                }
        }, kBlock);
        for (size_t p = 0; p < 2 * parts; ++p) scratchVerts_.insert(scratchVerts_.end(), scratchParts_[p].begin(), scratchParts_[p].end());
    }

    const size_t n = scratchVerts_.size();
    std::vector<std::pair<uint32_t, uint32_t>> bounds((n + kBlock - 1) / kBlock);
    poolFor(n, [&](size_t b, size_t e) {
        uint32_t lo = std::numeric_limits<uint32_t>::max(), hi = 0;
        for (size_t i = b; i < e; ++i) {
            const uint32_t v = scratchVerts_[i];
            glm::vec3 sum(0.0f);
            for (uint32_t ti : vertexTriangles(v)) sum += faceNormal(positions, triangles[ti]);
            const float len = glm::length(sum);
            if (len > 1e-20f) normals[v] = sum / len;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            AtomicRef<uint8_t>(dirtyBlocks[v >> kDirtyBlockShift]).store(1, std::memory_order_relaxed);
            AtomicRef<uint64_t>(dirtyBits[v >> 6]).fetch_or(uint64_t(1) << (v & 63), std::memory_order_relaxed);
        }
        bounds[b / kBlock] = {lo, hi + 1};
    }, kBlock);
    for (const auto& [lo, hi] : bounds) {
        dirtyBegin = std::min(dirtyBegin, lo);
        dirtyEnd = std::max(dirtyEnd, hi);
    }
}

int Mesh::addLayer(std::string name) {
    if (layers.empty()) base = positions;
    layers.push_back({std::move(name), std::vector<glm::vec3>(positions.size(), glm::vec3(0.0f)), 1.0f});
    return static_cast<int>(layers.size()) - 1;
}

void Mesh::removeLayer(int index) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    layers.erase(layers.begin() + index);
    if (layers.empty()) {
        positions = base;
        base.clear();
    } else {
        evaluateAll();
    }
    recomputeAllNormals();
}

void Mesh::setLayerWeight(int index, float weight) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    layers[index].weight = weight;
    evaluateAll();
    recomputeAllNormals();
}

std::vector<glm::vec3>& Mesh::layerTarget(int layer) {
    if (layers.empty()) return positions;
    if (layer < 0 || layer >= static_cast<int>(layers.size())) return base;
    return layers[layer].delta;
}

float Mesh::layerTargetWeight(int layer) const {
    if (layers.empty() || layer < 0 || layer >= static_cast<int>(layers.size())) return 1.0f;
    return layers[layer].weight;
}

void Mesh::evaluate(std::span<const uint32_t> verts) {
    if (layers.empty()) return;
    for (uint32_t v : verts) {
        glm::vec3 p = base[v];
        for (const auto& l : layers) p += l.weight * l.delta[v];
        positions[v] = p;
    }
}

void Mesh::evaluateAll() {
    if (layers.empty()) return;
    for (size_t v = 0; v < positions.size(); ++v) {
        glm::vec3 p = base[v];
        for (const auto& l : layers) p += l.weight * l.delta[v];
        positions[v] = p;
    }
}

AABB Mesh::bounds() const {
    AABB b;
    for (const auto& p : positions) b.expand(p);
    return b;
}

size_t Mesh::memoryBytes() const {
    auto bytes = [](const auto& v) { return v.capacity() * sizeof(v[0]); };
    size_t n = bytes(positions) + bytes(normals) + bytes(triangles) + bytes(mask) + bytes(colors) + bytes(polygroups) +
               bytes(hidden) + bytes(uvs) + bytes(quadPartners) + bytes(base) + bytes(paintBase) + bytes(adjOffsets_) +
               bytes(adjList_) + bytes(vtOffsets_) + bytes(vtList_) + bytes(vertStamp_) + bytes(scratchVerts_) + bytes(dirtyBlocks) + bytes(dirtyBits);
    for (const auto& l : layers) n += bytes(l.delta);
    for (const auto& l : paintLayers) n += bytes(l.color) + bytes(l.alpha);
    return n;
}

static void laplacianStep(Mesh& m, float lambda, std::vector<glm::vec3>& next) {
    next.resize(m.vertexCount());
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        auto ring = m.neighbors(v);
        if (ring.empty()) {
            next[v] = m.positions[v];
            continue;
        }
        glm::vec3 avg(0.0f);
        for (uint32_t n : ring) avg += m.positions[n];
        avg /= float(ring.size());
        next[v] = glm::mix(m.positions[v], avg, lambda);
    }
    m.positions.swap(next);
}

void taubinSmooth(Mesh& m, int iterations) {
    std::vector<glm::vec3> next;
    for (int it = 0; it < iterations; ++it) {
        laplacianStep(m, 0.5f, next);
        laplacianStep(m, -0.53f, next);
    }
}

Mesh weldImported(const Mesh& m, const std::vector<SculptLayer>& layers) {
    AABB box = m.bounds();
    const float tol = box.valid() ? std::max(glm::length(box.extent()) * 1e-6f, 1e-12f) : 1.0f;
    struct KeyHash {
        size_t operator()(const glm::ivec3& k) const {
            return size_t(k.x) * 73856093u ^ size_t(k.y) * 19349663u ^ size_t(k.z) * 83492791u;
        }
    };
    std::unordered_map<glm::ivec3, uint32_t, KeyHash> unique;
    unique.reserve(m.positions.size());
    std::vector<uint32_t> remap(m.positions.size());
    std::vector<uint32_t> kept;
    // Quantise relative to the box corner and clamp, so far-off, infinite or NaN coordinates from a malformed file
    // never overflow the int conversion.
    const glm::vec3 origin = box.valid() ? box.min : glm::vec3(0.0f);
    auto quantise = [&](float v) {
        return std::isfinite(v) ? int(std::clamp(std::round(v / tol), -2.0e9f, 2.0e9f)) : 0;
    };
    for (uint32_t v = 0; v < m.positions.size(); ++v) {
        const glm::vec3 d = m.positions[v] - origin;
        const glm::ivec3 key(quantise(d.x), quantise(d.y), quantise(d.z));
        auto [it, inserted] = unique.try_emplace(key, uint32_t(kept.size()));
        if (inserted) kept.push_back(v);
        remap[v] = it->second;
    }
    Mesh welded;
    for (uint32_t v : kept) welded.positions.push_back(m.positions[v]);
    const bool vMask = m.mask.size() == m.positions.size(), vColors = m.colors.size() == m.positions.size();
    for (uint32_t v : kept) {
        if (vMask) welded.mask.push_back(m.mask[v]);
        if (vColors) welded.colors.push_back(m.colors[v]);
    }
    const size_t nt = m.triangles.size();
    const bool tGroups = m.polygroups.size() == nt, tHidden = m.hidden.size() == nt, tUvs = m.uvs.size() == nt * 3;
    std::vector<uint32_t> newTriangle(m.hasQuads() ? nt : 0, Mesh::kNoQuad);
    for (size_t ti = 0; ti < nt; ++ti) {
        const auto& t = m.triangles[ti];
        glm::uvec3 nt3(remap[t.x], remap[t.y], remap[t.z]);
        if (nt3.x == nt3.y || nt3.y == nt3.z || nt3.x == nt3.z) continue;
        if (!newTriangle.empty()) newTriangle[ti] = uint32_t(welded.triangles.size());
        welded.triangles.push_back(nt3);
        if (tGroups) welded.polygroups.push_back(m.polygroups[ti]);
        if (tHidden) welded.hidden.push_back(m.hidden[ti]);
        if (tUvs) welded.uvs.insert(welded.uvs.end(), m.uvs.begin() + std::ptrdiff_t(ti * 3), m.uvs.begin() + std::ptrdiff_t(ti * 3 + 3));
    }
    if (!newTriangle.empty()) remapQuads(m, newTriangle, welded);
    if (!layers.empty()) {
        welded.base = welded.positions;
        for (const auto& src : layers) {
            SculptLayer layer{src.name, {}, src.weight};
            layer.delta.reserve(kept.size());
            for (uint32_t v : kept) layer.delta.push_back(v < src.delta.size() ? src.delta[v] : glm::vec3(0.0f));
            welded.layers.push_back(std::move(layer));
        }
        welded.evaluateAll();
    }
    welded.rebuildTopology();
    welded.recomputeAllNormals();
    return welded;
}

void remapQuads(const Mesh& src, const std::vector<uint32_t>& newIndex, Mesh& dst) {
    const size_t nt = src.triangleCount();
    if (src.quadPartners.size() != nt || newIndex.size() != nt) return;
    std::vector<uint32_t> partners(dst.triangleCount(), Mesh::kNoQuad);
    bool any = false;
    for (size_t t = 0; t < nt; ++t) {
        const uint32_t u = src.quadPartners[t], nt0 = newIndex[t];
        if (u >= nt || nt0 >= partners.size() || newIndex[u] >= partners.size()) continue;
        partners[nt0] = newIndex[u];
        any = true;
    }
    if (any) dst.quadPartners = std::move(partners);
    else dst.clearQuads();
}

void appendQuads(Mesh& dst, size_t firstTriangle, const Mesh& src) {
    const size_t st = src.triangleCount();
    const bool srcQuads = st > 0 && src.quadPartners.size() == st;
    if (!srcQuads && dst.quadPartners.empty()) return;
    dst.quadPartners.resize(firstTriangle, Mesh::kNoQuad);
    for (size_t t = 0; t < st; ++t) {
        const uint32_t u = srcQuads ? src.quadPartners[t] : Mesh::kNoQuad;
        dst.quadPartners.push_back(u == Mesh::kNoQuad ? u : uint32_t(u + firstTriangle));
    }
}

glm::vec3 srgbToLinear(const glm::vec3& c) {
    glm::vec3 out;
    for (int i = 0; i < 3; ++i) {
        const float x = std::clamp(c[i], 0.0f, 1.0f);
        out[i] = x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
    }
    return out;
}

glm::vec3 linearToSrgb(const glm::vec3& c) {
    glm::vec3 out;
    for (int i = 0; i < 3; ++i) {
        const float x = std::clamp(c[i], 0.0f, 1.0f);
        out[i] = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
    }
    return out;
}

void transferVertexAttributes(const Mesh& src, Mesh& dst) {
    const bool wantMask = src.mask.size() == src.positions.size() && !src.mask.empty();
    const bool wantColors = src.colors.size() == src.positions.size() && !src.colors.empty();
    if ((!wantMask && !wantColors) || dst.positions.empty()) return;

    // Uniform hash grid with about one source vertex per cell.
    const AABB box = src.bounds();
    const glm::vec3 ext = box.extent();
    const float longest = std::max({ext.x, ext.y, ext.z, 1e-12f});
    const float cell = std::max(longest / std::max(1.0f, std::cbrt(float(src.positions.size()))), longest * 1e-4f);
    constexpr int kOffset = 1 << 20;
    // Clamped so NaN, infinite or huge coordinates from a malformed file never overflow the int cast.
    auto cellOf = [&](const glm::vec3& p) {
        const glm::vec3 f = glm::floor((p - box.min) / cell);
        auto axis = [](float v) { return std::isfinite(v) ? int(std::clamp(v, -float(kOffset), float(kOffset))) : 0; };
        return glm::ivec3(axis(f.x), axis(f.y), axis(f.z));
    };
    auto key = [](const glm::ivec3& c) {
        return (uint64_t(uint32_t(c.x + kOffset) & 0x1fffffu) << 42) | (uint64_t(uint32_t(c.y + kOffset) & 0x1fffffu) << 21) |
               uint64_t(uint32_t(c.z + kOffset) & 0x1fffffu);
    };
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    grid.reserve(src.positions.size());
    for (uint32_t v = 0; v < src.positions.size(); ++v) grid[key(cellOf(src.positions[v]))].push_back(v);
    const int gridCells = int(std::ceil(longest / cell)) + 2;

    if (wantMask) dst.mask.assign(dst.positions.size(), 0.0f);
    if (wantColors) dst.colors.assign(dst.positions.size(), glm::vec3(1.0f));
    for (size_t i = 0; i < dst.positions.size(); ++i) {
        const glm::vec3 p = dst.positions[i];
        const glm::ivec3 c = cellOf(p);
        const glm::vec3 outside = glm::max(glm::max(box.min - p, p - box.max), glm::vec3(0.0f));
        const int maxRing = gridCells + int(std::ceil(glm::length(outside) / cell));
        float best = std::numeric_limits<float>::max();
        uint32_t bestV = 0;
        // Search shells of growing Chebyshev radius; stop once nothing closer can remain.
        for (int r = 0; r <= maxRing; ++r) {
            for (int dz = -r; dz <= r; ++dz)
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != r) continue;
                        auto it = grid.find(key(c + glm::ivec3(dx, dy, dz)));
                        if (it == grid.end()) continue;
                        for (uint32_t v : it->second) {
                            const glm::vec3 d = src.positions[v] - p;
                            const float d2 = glm::dot(d, d);
                            if (d2 < best) {
                                best = d2;
                                bestV = v;
                            }
                        }
                    }
            if (best < std::numeric_limits<float>::max() && std::sqrt(best) <= float(r) * cell) break;
        }
        if (wantMask) dst.mask[i] = src.mask[bestV];
        if (wantColors) dst.colors[i] = src.colors[bestV];
    }
    dst.markAllDirty();
}

} // namespace sf
