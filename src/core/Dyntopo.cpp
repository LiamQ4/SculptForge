// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Dyntopo.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace sf {

namespace {

constexpr uint32_t kNone = ~0u;

// Calls f(meshArray, storedArray) for every per-vertex array of `m` an undo keeps (normals are recomputed).
template <class F>
void zipVertexArrays(Mesh& m, TopologyVertexValues& v, F&& f) {
    f(m.positions, v.positions);
    f(m.mask, v.mask);
    f(m.colors, v.colors);
    f(m.base, v.base);
    f(m.paintBase, v.paintBase);
    v.deltas.resize(m.layers.size());
    for (size_t i = 0; i < m.layers.size(); ++i) f(m.layers[i].delta, v.deltas[i]);
    v.paintColors.resize(m.paintLayers.size());
    v.paintAlphas.resize(m.paintLayers.size());
    for (size_t i = 0; i < m.paintLayers.size(); ++i) {
        f(m.paintLayers[i].color, v.paintColors[i]);
        f(m.paintLayers[i].alpha, v.paintAlphas[i]);
    }
}

template <class F>
void zipTriangleArrays(Mesh& m, TopologyTriangleValues& t, F&& f) {
    f(m.triangles, t.triangles);
    f(m.polygroups, t.polygroups);
    f(m.quadPartners, t.quadPartners);
    f(m.hidden, t.hidden);
}

// Calls f(array) for every per-vertex array of `m` (normals included).
template <class F>
void forVertexArrays(Mesh& m, F&& f) {
    f(m.positions);
    f(m.normals);
    f(m.mask);
    f(m.colors);
    f(m.base);
    f(m.paintBase);
    for (auto& l : m.layers) f(l.delta);
    for (auto& l : m.paintLayers) {
        f(l.color);
        f(l.alpha);
    }
}

template <class F>
void forTriangleArrays(Mesh& m, F&& f) {
    f(m.triangles);
    f(m.polygroups);
    f(m.quadPartners);
    f(m.hidden);
}

// Exchanges the values of `a` at `slots` with `stored` (the values of another state at the slots below
// `count`, in slot order) and resizes `a` to `count`. Absent arrays stay absent.
template <class T>
void swapSlots(std::vector<T>& a, const std::vector<uint32_t>& slots, std::vector<T>& stored, size_t count) {
    if (a.empty()) return;
    std::vector<T> current;
    current.reserve(slots.size());
    for (uint32_t s : slots)
        if (s < a.size()) current.push_back(a[s]);
    a.resize(count);
    size_t k = 0;
    for (uint32_t s : slots)
        if (s < count && k < stored.size()) a[s] = stored[k++];
    stored.swap(current);
}

template <class T>
size_t bytesOf(const std::vector<T>& v) {
    return v.size() * sizeof(T);
}

bool setBit(std::vector<uint64_t>& bits, uint32_t i) {
    if (size_t(i >> 6) >= bits.size()) bits.resize(size_t(i >> 6) + 1 + bits.size() / 4, 0);
    const uint64_t b = uint64_t(1) << (i & 63);
    if (bits[i >> 6] & b) return false;
    bits[i >> 6] |= b;
    return true;
}

float quality(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    // 1 for an equilateral triangle, 0 for a degenerate one.
    const float area2 = glm::length(glm::cross(b - a, c - a));
    const float sum = glm::dot(b - a, b - a) + glm::dot(c - b, c - b) + glm::dot(a - c, a - c);
    return sum > 0.0f ? 3.4641016f * area2 / sum : 0.0f; // 4 sqrt(3) * area / sum, area = area2 / 2
}

} // namespace

float dyntopoEdgeLength(const DyntopoSettings& s, float radius, float diagonal) {
    const float d = std::clamp(s.detail, 0.0f, 1.0f);
    if (s.constantDetail) return std::max(diagonal, 1e-6f) * 0.05f * std::pow(0.04f, d); // diag/20 .. diag/500
    return std::max(radius, 1e-7f) * 0.4f * std::pow(0.1f, d);                          // 0.4 .. 0.04 x the radius
}

std::string dyntopoBlocker(const Mesh& mesh) {
    if (mesh.multires || mesh.implicitAdjacency())
        return "Dynamic topology needs a mesh without subdivision levels: delete the lower (or higher) levels first";
    if (mesh.triangleCount() == 0) return "The mesh has no triangles";
    return {};
}

size_t TopologyVertexValues::bytes() const {
    size_t n = bytesOf(positions) + bytesOf(colors) + bytesOf(base) + bytesOf(paintBase) + bytesOf(mask);
    for (const auto& d : deltas) n += bytesOf(d);
    for (const auto& d : paintColors) n += bytesOf(d);
    for (const auto& d : paintAlphas) n += bytesOf(d);
    return n;
}

size_t TopologyTriangleValues::bytes() const {
    return bytesOf(triangles) + bytesOf(polygroups) + bytesOf(quadPartners) + bytesOf(hidden);
}

size_t TopologyUndo::bytes() const {
    return vertices.bytes() + triangles.bytes() + bytesOf(vertexSlots) + bytesOf(triangleSlots) + bytesOf(uvs);
}

void TopologyUndo::swapWith(Mesh& mesh) {
    const uint32_t nv = uint32_t(mesh.vertexCount()), nt = uint32_t(mesh.triangleCount());
    zipVertexArrays(mesh, vertices, [&](auto& a, auto& stored) { swapSlots(a, vertexSlots, stored, vertexCount); });
    zipTriangleArrays(mesh, triangles, [&](auto& a, auto& stored) { swapSlots(a, triangleSlots, stored, triangleCount); });
    mesh.normals.resize(vertexCount, glm::vec3(0.0f, 1.0f, 0.0f));
    std::swap(uvs, mesh.uvs);
    vertexCount = nv;
    triangleCount = nt;
}

// ---------------------------------------------------------------------------
// Recording and elements
// ---------------------------------------------------------------------------

void DynamicTopology::recordVertex(uint32_t v) {
    if (!setBit(vertRec_, v)) return;
    undo_->vertexSlots.push_back(v);
    recordedNow_.push_back(v);
    if (v < startVertices_)
        zipVertexArrays(*mesh_, undo_->vertices, [&](auto& a, auto& stored) {
            if (!a.empty()) stored.push_back(a[v]);
        });
}

void DynamicTopology::recordTriangle(uint32_t t) {
    if (!setBit(triRec_, t)) return;
    undo_->triangleSlots.push_back(t);
    if (t < startTriangles_)
        zipTriangleArrays(*mesh_, undo_->triangles, [&](auto& a, auto& stored) {
            if (!a.empty()) stored.push_back(a[t]);
        });
}

void DynamicTopology::markVertex(uint32_t v) {
    if (v >= changedVFlag_.size()) changedVFlag_.resize(size_t(v) + 1, 0);
    if (changedVFlag_[v]) return;
    changedVFlag_[v] = 1;
    changedV_.push_back(v);
}

void DynamicTopology::markTriangle(uint32_t t) { changedT_.push_back(t); }

uint32_t DynamicTopology::newVertex() {
    Mesh& m = *mesh_;
    if (!freeV_.empty()) {
        const uint32_t v = freeV_.back();
        freeV_.pop_back();
        deadV_[v] = 0;
        return v; // (recorded when it died)
    }
    const uint32_t v = uint32_t(m.vertexCount());
    recordVertex(v);
    forVertexArrays(m, [&](auto& a) {
        if (!a.empty()) a.emplace_back();
    });
    m.normals.resize(m.positions.size(), glm::vec3(0.0f, 1.0f, 0.0f)); // (a mesh without normals gets them)
    m.addVertexSlot();
    deadV_.push_back(0);
    return v;
}

void DynamicTopology::interpolateVertex(uint32_t v, uint32_t a, uint32_t b, float t, const glm::vec3& position) {
    Mesh& m = *mesh_;
    recordVertex(v);
    forVertexArrays(m, [&](auto& arr) {
        if (!arr.empty()) arr[v] = arr[a] + (arr[b] - arr[a]) * t;
    });
    m.positions[v] = position;
    const glm::vec3 n = m.normals[v];
    const float len = glm::length(n);
    m.normals[v] = len > 1e-20f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
    markVertex(v);
}

void DynamicTopology::killVertex(uint32_t v) {
    recordVertex(v);
    deadV_[v] = 1;
    freeV_.push_back(v);
    mesh_->setVertexTriangles(v, {});
    mesh_->setNeighbors(v, {});
    for (size_t k = 0; k < mirrorMaps_.size(); ++k) {
        auto it = mirrorMaps_[k].find(v);
        if (it == mirrorMaps_[k].end()) continue;
        const uint32_t w = it->second;
        mirrorMaps_[k].erase(it);
        auto back = mirrorMaps_[k].find(w);
        if (back != mirrorMaps_[k].end() && back->second == v) mirrorMaps_[k].erase(back);
    }
}

void DynamicTopology::vtInsert(uint32_t v, uint32_t t) {
    const IndexRing r = mesh_->vertexTriangles(v);
    scratch_.assign(r.begin(), r.end());
    scratch_.insert(std::upper_bound(scratch_.begin(), scratch_.end(), t), t);
    mesh_->setVertexTriangles(v, scratch_);
}

void DynamicTopology::vtErase(uint32_t v, uint32_t t) {
    const IndexRing r = mesh_->vertexTriangles(v);
    scratch_.assign(r.begin(), r.end());
    auto it = std::lower_bound(scratch_.begin(), scratch_.end(), t);
    if (it == scratch_.end() || *it != t) return;
    scratch_.erase(it);
    mesh_->setVertexTriangles(v, scratch_);
}

void DynamicTopology::vtReplace(uint32_t v, uint32_t from, uint32_t to) {
    const IndexRing r = mesh_->vertexTriangles(v);
    scratch_.assign(r.begin(), r.end());
    auto it = std::lower_bound(scratch_.begin(), scratch_.end(), from);
    if (it != scratch_.end() && *it == from) scratch_.erase(it);
    scratch_.insert(std::upper_bound(scratch_.begin(), scratch_.end(), to), to);
    mesh_->setVertexTriangles(v, scratch_);
}

void DynamicTopology::unpairQuad(uint32_t t) {
    Mesh& m = *mesh_;
    if (m.quadPartners.empty() || t >= m.quadPartners.size()) return;
    const uint32_t p = m.quadPartners[t];
    if (p == Mesh::kNoQuad) return;
    recordTriangle(t);
    m.quadPartners[t] = Mesh::kNoQuad;
    if (p < m.quadPartners.size() && m.quadPartners[p] == t) {
        recordTriangle(p);
        m.quadPartners[p] = Mesh::kNoQuad;
        markTriangle(p); // its wireframe now shows the old diagonal
    }
}

uint32_t DynamicTopology::newTriangle(const glm::uvec3& tri, uint32_t like) {
    Mesh& m = *mesh_;
    uint32_t t;
    if (!freeT_.empty()) {
        t = freeT_.back();
        freeT_.pop_back();
        deadT_[t] = 0;
    } else {
        t = uint32_t(m.triangleCount());
        recordTriangle(t);
        forTriangleArrays(m, [&](auto& a) {
            if (!a.empty()) a.emplace_back();
        });
        deadT_.push_back(0);
    }
    recordTriangle(t);
    m.triangles[t] = tri;
    if (!m.polygroups.empty()) m.polygroups[t] = m.polygroups[like];
    if (!m.hidden.empty()) m.hidden[t] = m.hidden[like];
    if (!m.quadPartners.empty()) m.quadPartners[t] = Mesh::kNoQuad;
    for (int c = 0; c < 3; ++c) {
        vtInsert(tri[c], t);
        markVertex(tri[c]);
    }
    bvh_->insertTriangle(m, t, like);
    markTriangle(t);
    return t;
}

void DynamicTopology::setTriangle(uint32_t t, const glm::uvec3& tri) {
    Mesh& m = *mesh_;
    recordTriangle(t);
    unpairQuad(t);
    const glm::uvec3 old = m.triangles[t];
    auto has = [](const glm::uvec3& x, uint32_t v) { return x.x == v || x.y == v || x.z == v; };
    for (int c = 0; c < 3; ++c) {
        if (!has(tri, old[c])) vtErase(old[c], t);
        markVertex(old[c]);
    }
    for (int c = 0; c < 3; ++c) {
        if (!has(old, tri[c])) vtInsert(tri[c], t);
        markVertex(tri[c]);
    }
    m.triangles[t] = tri;
    bvh_->touchTriangle(t);
    markTriangle(t);
}

void DynamicTopology::killTriangle(uint32_t t) {
    Mesh& m = *mesh_;
    recordTriangle(t);
    unpairQuad(t);
    const glm::uvec3 tri = m.triangles[t];
    for (int c = 0; c < 3; ++c) {
        vtErase(tri[c], t);
        markVertex(tri[c]);
    }
    bvh_->removeTriangle(t);
    deadT_[t] = 1;
    freeT_.push_back(t);
    markTriangle(t);
}

void DynamicTopology::moveTriangle(uint32_t from, uint32_t to) {
    Mesh& m = *mesh_;
    recordTriangle(to);
    recordTriangle(from);
    forTriangleArrays(m, [&](auto& a) {
        if (!a.empty()) a[to] = a[from];
    });
    if (!m.quadPartners.empty()) {
        const uint32_t p = m.quadPartners[to];
        if (p != Mesh::kNoQuad && p < m.quadPartners.size() && m.quadPartners[p] == from) {
            recordTriangle(p);
            m.quadPartners[p] = to;
        }
    }
    const glm::uvec3 tri = m.triangles[to];
    for (int c = 0; c < 3; ++c) vtReplace(tri[c], from, to);
    bvh_->renumberTriangle(from, to);
    markTriangle(to);
    markTriangle(from);
}

void DynamicTopology::moveVertex(uint32_t from, uint32_t to) {
    Mesh& m = *mesh_;
    recordVertex(to);
    recordVertex(from);
    forVertexArrays(m, [&](auto& a) {
        if (!a.empty()) a[to] = a[from];
    });
    const IndexRing tr = m.vertexTriangles(from);
    scratch2_.assign(tr.begin(), tr.end());
    m.setVertexTriangles(to, scratch2_);
    for (uint32_t t : scratch2_) {
        recordTriangle(t);
        glm::uvec3& tri = m.triangles[t];
        for (int c = 0; c < 3; ++c) {
            if (tri[c] == from) tri[c] = to;
            else markVertex(tri[c]); // its ring names `from`
        }
        markTriangle(t);
    }
    m.setVertexTriangles(from, {});
    markVertex(to);
    moved_.emplace_back(from, to);
}

// Fills the slots this remesh freed with the elements at the end of the arrays, so the mesh has no dead
// entries; then drops the end.
void DynamicTopology::compact() {
    Mesh& m = *mesh_;
    std::sort(freeT_.begin(), freeT_.end());
    size_t nt = m.triangleCount();
    for (uint32_t t : freeT_) {
        while (nt > 0 && deadT_[nt - 1]) --nt;
        if (t >= nt) break;
        moveTriangle(uint32_t(nt - 1), t);
        deadT_[t] = 0;
        --nt;
    }
    forTriangleArrays(m, [&](auto& a) {
        if (!a.empty()) a.resize(nt);
    });
    bvh_->resizeTriangles(nt);
    deadT_.assign(nt, 0);
    freeT_.clear();

    std::sort(freeV_.begin(), freeV_.end());
    const size_t before = m.vertexCount();
    size_t nv = before;
    for (uint32_t v : freeV_) {
        while (nv > 0 && deadV_[nv - 1]) --nv;
        if (v >= nv) break;
        moveVertex(uint32_t(nv - 1), v);
        deadV_[v] = 0;
        --nv;
    }
    forVertexArrays(m, [&](auto& a) {
        if (!a.empty()) a.resize(nv);
    });
    for (size_t i = nv; i < before; ++i) m.removeLastVertexSlot();
    deadV_.assign(nv, 0);
    freeV_.clear();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

int DynamicTopology::edgeTriangles(uint32_t a, uint32_t b, uint32_t* out) const {
    int n = 0;
    for (uint32_t t : mesh_->vertexTriangles(a)) {
        const glm::uvec3& tri = mesh_->triangles[t];
        if (tri.x == b || tri.y == b || tri.z == b) {
            if (n < 4) out[n] = t;
            ++n;
        }
    }
    return n;
}

void DynamicTopology::ring(uint32_t v, std::vector<uint32_t>& out) const {
    out.clear();
    for (uint32_t t : mesh_->vertexTriangles(v)) {
        const glm::uvec3& tri = mesh_->triangles[t];
        for (int c = 0; c < 3; ++c)
            if (tri[c] != v) out.push_back(tri[c]);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

float DynamicTopology::len2(uint32_t a, uint32_t b) const {
    const glm::vec3 d = mesh_->positions[a] - mesh_->positions[b];
    return glm::dot(d, d);
}

// The edge passes within the brush radius of the dab's centre.
bool DynamicTopology::inside(uint32_t a, uint32_t b) const {
    const glm::vec3 p = mesh_->positions[a], d = mesh_->positions[b] - p;
    const float dd = glm::dot(d, d);
    const float t = dd > 0.0f ? std::clamp(glm::dot(center_ - p, d) / dd, 0.0f, 1.0f) : 0.0f;
    const glm::vec3 q = p + d * t - center_;
    return glm::dot(q, q) <= radius_ * radius_;
}

// Left alone: fully masked, next to a hidden triangle, on a non-manifold edge, or without triangles.
bool DynamicTopology::locked(uint32_t v) const {
    const Mesh& m = *mesh_;
    if (deadV_[v]) return true;
    if (m.maskAt(v) >= 1.0f - 1e-4f) return true;
    const IndexRing tris = m.vertexTriangles(v);
    if (tris.empty()) return true;
    if (m.hasHidden())
        for (uint32_t t : tris)
            if (m.hidden[t]) return true;
    // Each neighbour shares one (boundary) or two triangles with v; more is non-manifold.
    uint32_t count[64], other[64];
    int n = 0;
    for (uint32_t t : tris) {
        const glm::uvec3& tri = m.triangles[t];
        for (int c = 0; c < 3; ++c) {
            if (tri[c] == v) continue;
            int i = 0;
            while (i < n && other[i] != tri[c]) ++i;
            if (i == n) {
                if (n == 64) return true;
                other[n] = tri[c];
                count[n++] = 0;
            }
            if (++count[i] > 2) return true;
        }
    }
    return false;
}

bool DynamicTopology::planeVertex(uint32_t v) const {
    for (size_t k = 0; k < mirrorMaps_.size(); ++k)
        if (mirrorOf(k, v) == v) return true;
    return false;
}

// Boundary, polygroup border, non-manifold, or lying in a symmetry plane.
bool DynamicTopology::featureEdge(uint32_t a, uint32_t b) const {
    uint32_t tris[4];
    const int n = edgeTriangles(a, b, tris);
    if (n != 2) return true;
    if (mesh_->polygroupAt(tris[0]) != mesh_->polygroupAt(tris[1])) return true;
    for (size_t k = 0; k < mirrorMaps_.size(); ++k)
        if (mirrorOf(k, a) == a && mirrorOf(k, b) == b) return true;
    return false;
}

// The number of feature edges at v (the other ends of the first two in `other`), or 3 for a corner: a vertex
// in a symmetry plane is a corner unless exactly two of its edges lie in that plane.
int DynamicTopology::featureEdges(uint32_t v, uint32_t* other) const {
    ring(v, scratch_);
    int n = 0;
    for (uint32_t u : scratch_)
        if (featureEdge(v, u)) {
            if (n < 2) other[n] = u;
            ++n;
        }
    if (n == 0 && planeVertex(v)) return 3;
    return std::min(n, 3);
}

bool DynamicTopology::boundaryVertex(uint32_t v) const {
    ring(v, scratch2_);
    uint32_t tris[4];
    for (uint32_t u : scratch2_)
        if (edgeTriangles(v, u, tris) == 1) return true;
    return false;
}

glm::vec3 DynamicTopology::faceNormalAt(uint32_t v) const {
    glm::vec3 n(0.0f);
    const auto& p = mesh_->positions;
    for (uint32_t t : mesh_->vertexTriangles(v)) {
        const glm::uvec3& tri = mesh_->triangles[t];
        n += glm::cross(p[tri.y] - p[tri.x], p[tri.z] - p[tri.x]);
    }
    const float len = glm::length(n);
    return len > 1e-30f ? n / len : glm::vec3(0.0f);
}

// ---------------------------------------------------------------------------
// Symmetry
// ---------------------------------------------------------------------------

uint32_t DynamicTopology::mirrorOf(size_t k, uint32_t v) const {
    const auto it = mirrorMaps_[k].find(v);
    return it == mirrorMaps_[k].end() ? kNone : it->second;
}

void DynamicTopology::setMirror(size_t k, uint32_t v, uint32_t w) {
    mirrorMaps_[k][v] = w;
    mirrorMaps_[k][w] = v;
}

// Pairs each vertex with the vertex at its mirror image (within eps_), for every mirror; only pairs that
// find each other both ways count.
void DynamicTopology::matchMirrors(const std::vector<uint32_t>& verts) {
    const Mesh& m = *mesh_;
    const float cell = std::max(eps_ * 4.0f, 1e-12f);
    auto key = [cell](const glm::vec3& p) {
        const glm::ivec3 q = glm::ivec3(glm::floor(p / cell));
        return (uint64_t(uint32_t(q.x) & 0x1FFFFF) << 42) | (uint64_t(uint32_t(q.y) & 0x1FFFFF) << 21) |
               uint64_t(uint32_t(q.z) & 0x1FFFFF);
    };
    std::unordered_multimap<uint64_t, uint32_t> grid;
    grid.reserve(verts.size() * 2);
    for (uint32_t v : verts) grid.emplace(key(m.positions[v]), v);
    auto nearest = [&](const glm::vec3& p) {
        uint32_t best = kNone;
        float bestD = eps_ * eps_;
        const glm::ivec3 q = glm::ivec3(glm::floor(p / cell));
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const glm::ivec3 c = q + glm::ivec3(dx, dy, dz);
                    const uint64_t k = (uint64_t(uint32_t(c.x) & 0x1FFFFF) << 42) | (uint64_t(uint32_t(c.y) & 0x1FFFFF) << 21) |
                                       uint64_t(uint32_t(c.z) & 0x1FFFFF);
                    auto [b, e] = grid.equal_range(k);
                    for (auto it = b; it != e; ++it) {
                        const glm::vec3 d = m.positions[it->second] - p;
                        const float dd = glm::dot(d, d);
                        if (dd <= bestD && (best == kNone || dd < bestD || it->second < best)) {
                            bestD = dd;
                            best = it->second;
                        }
                    }
                }
        return best;
    };
    for (size_t k = 0; k < mirrorSigns_.size(); ++k) {
        auto& map = mirrorMaps_[k];
        map.clear();
        const glm::vec3 s = mirrorSigns_[k];
        for (uint32_t v : verts) {
            const uint32_t w = nearest(m.positions[v] * s);
            if (w == kNone) continue;
            if (nearest(m.positions[w] * s) != v) continue;
            map[v] = w;
        }
    }
}

// The edge a-b and its distinct mirror images (each once, either direction).
void DynamicTopology::orbit(uint32_t a, uint32_t b, std::vector<std::pair<uint32_t, uint32_t>>& out) const {
    out.assign(1, {a, b});
    orbitIncomplete_ = false;
    uint32_t tris[4];
    for (size_t k = 0; k < mirrorMaps_.size(); ++k) {
        const uint32_t ma = mirrorOf(k, a), mb = mirrorOf(k, b);
        if (ma == kNone || mb == kNone || ma == mb || edgeTriangles(ma, mb, tris) == 0) {
            orbitIncomplete_ = true;
            continue;
        }
        bool seen = false;
        for (const auto& e : out) seen = seen || (e.first == ma && e.second == mb) || (e.first == mb && e.second == ma);
        if (!seen && edgeTriangles(ma, mb, tris) > 0) out.emplace_back(ma, mb);
    }
}

bool DynamicTopology::orbitSharesTriangle(const std::vector<std::pair<uint32_t, uint32_t>>& edges) const {
    uint32_t ti[4], tj[4];
    for (size_t i = 0; i < edges.size(); ++i) {
        const int ni = std::min(4, edgeTriangles(edges[i].first, edges[i].second, ti));
        for (size_t j = i + 1; j < edges.size(); ++j) {
            const int nj = std::min(4, edgeTriangles(edges[j].first, edges[j].second, tj));
            for (int x = 0; x < ni; ++x)
                for (int y = 0; y < nj; ++y)
                    if (ti[x] == tj[y]) return true;
            // Sharing a vertex is enough to make the order of the two operations matter.
            const auto& e = edges[i];
            const auto& f = edges[j];
            if (e.first == f.first || e.first == f.second || e.second == f.first || e.second == f.second) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

// Splits edge a-b at its midpoint: each triangle on it becomes two. Returns the new vertex (kNone if the
// edge isn't a manifold one).
uint32_t DynamicTopology::split(uint32_t a, uint32_t b) {
    Mesh& m = *mesh_;
    uint32_t tris[4];
    const int n = edgeTriangles(a, b, tris);
    if (n < 1 || n > 2) return kNone;
    const glm::vec3 mid = (m.positions[a] + m.positions[b]) * 0.5f;
    const uint32_t v = newVertex();
    interpolateVertex(v, a, b, 0.5f, mid);
    for (int i = 0; i < n; ++i) {
        const uint32_t t = tris[i];
        glm::uvec3 tri = m.triangles[t];
        int k = 0;
        while (k < 3 && !((tri[k] == a && tri[(k + 1) % 3] == b) || (tri[k] == b && tri[(k + 1) % 3] == a))) ++k;
        if (k == 3) continue;
        const uint32_t x = tri[k], y = tri[(k + 1) % 3], c = tri[(k + 2) % 3];
        glm::uvec3 first = tri;
        first[(k + 1) % 3] = v; // (x, v, c) in t's own rotation
        setTriangle(t, first);
        newTriangle(glm::uvec3(v, y, c), t);
        (void)x;
    }
    ++stats.splits;
    ++ops_;
    return v;
}

void DynamicTopology::pushEdgesAround(uint32_t v, bool longer) {
    ring(v, scratch2_);
    for (uint32_t u : scratch2_) {
        if (deadV_[u] || !inside(v, u)) continue;
        const float l = len2(v, u);
        if (longer ? l > splitLen2_ : l < collapseLen2_) {
            heap_.push_back({longer ? l : -l, std::min(u, v), std::max(u, v)});
            std::push_heap(heap_.begin(), heap_.end(), [](const Edge& x, const Edge& y) {
                return x.len2 != y.len2 ? x.len2 < y.len2 : x.a != y.a ? x.a > y.a : x.b > y.b;
            });
        }
    }
}

void DynamicTopology::splitOrbit(uint32_t a, uint32_t b, int depth) {
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    orbit(a, b, edges);
    // Two images of the edge on one triangle (a triangle across the symmetry plane): split its third edge,
    // which crosses the plane, first, so the two images end up apart and the result stays mirrored.
    if (edges.size() > 1 && depth < 3) {
        uint32_t ti[4], tj[4];
        bool again = false;
        for (size_t i = 0; i < edges.size() && !again; ++i) {
            const int ni = std::min(4, edgeTriangles(edges[i].first, edges[i].second, ti));
            for (size_t j = i + 1; j < edges.size() && !again; ++j) {
                const int nj = std::min(4, edgeTriangles(edges[j].first, edges[j].second, tj));
                for (int x = 0; x < ni && !again; ++x)
                    for (int y = 0; y < nj && !again; ++y) {
                        if (ti[x] != tj[y]) continue;
                        const glm::uvec3 tri = mesh_->triangles[ti[x]];
                        // The corner on neither edge... the third edge joins the two corners other than the shared one.
                        uint32_t shared = kNone;
                        for (uint32_t c : {edges[i].first, edges[i].second})
                            if (c == edges[j].first || c == edges[j].second) shared = c;
                        if (shared == kNone) continue;
                        uint32_t p = kNone, q = kNone;
                        for (int c = 0; c < 3; ++c)
                            if (tri[c] != shared) (p == kNone ? p : q) = tri[c];
                        if (p != kNone && q != kNone && !locked(p) && !locked(q)) {
                            splitOrbit(p, q, depth + 1);
                            again = true;
                        }
                    }
            }
        }
        if (again) orbit(a, b, edges);
    }
    if (orbitIncomplete_ && !mirrorMaps_.empty()) ++stats.unpaired;
    std::vector<uint32_t> made(edges.size(), kNone);
    uint32_t tris[4];
    for (size_t i = 0; i < edges.size(); ++i) {
        const auto [x, y] = edges[i];
        const int n = edgeTriangles(x, y, tris);
        if (n < 1 || n > 2 || locked(x) || locked(y)) continue;
        made[i] = split(x, y);
    }
    // The new vertices are each other's mirror images as their edges were.
    for (size_t k = 0; k < mirrorMaps_.size(); ++k)
        for (size_t i = 0; i < edges.size(); ++i) {
            if (made[i] == kNone) continue;
            const uint32_t ma = mirrorOf(k, edges[i].first), mb = mirrorOf(k, edges[i].second);
            for (size_t j = 0; j < edges.size(); ++j)
                if (made[j] != kNone && ((edges[j].first == ma && edges[j].second == mb) ||
                                         (edges[j].first == mb && edges[j].second == ma)))
                    setMirror(k, made[i], made[j]);
        }
    for (uint32_t v : made)
        if (v != kNone) pushEdgesAround(v, true);
}

void DynamicTopology::splitPass() {
    heap_.clear();
    uint32_t tris[4];
    for (uint32_t t : regionTris_) {
        const glm::uvec3 tri = mesh_->triangles[t];
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = tri[c], b = tri[(c + 1) % 3];
            // Each edge once: from the triangle where it runs upwards (a boundary edge from its only one).
            if (a > b && edgeTriangles(a, b, tris) != 1) continue;
            const float l = len2(a, b);
            if (l > splitLen2_ && inside(a, b)) heap_.push_back({l, std::min(a, b), std::max(a, b)});
        }
    }
    auto less = [](const Edge& x, const Edge& y) {
        return x.len2 != y.len2 ? x.len2 < y.len2 : x.a != y.a ? x.a > y.a : x.b > y.b;
    };
    std::make_heap(heap_.begin(), heap_.end(), less);
    while (!heap_.empty() && ops_ < maxOps_) {
        std::pop_heap(heap_.begin(), heap_.end(), less);
        const Edge e = heap_.back();
        heap_.pop_back();
        if (deadV_[e.a] || deadV_[e.b]) continue;
        const int n = edgeTriangles(e.a, e.b, tris);
        if (n < 1 || n > 2 || len2(e.a, e.b) <= splitLen2_) continue;
        if (locked(e.a) || locked(e.b)) continue;
        splitOrbit(e.a, e.b, 0);
    }
}

// Which end of edge a-b goes and where the other one ends up: a free vertex (no feature edges) goes onto a
// feature vertex, or both meet at the midpoint; a vertex on a feature curve (two feature edges) only slides
// along it onto the next vertex of the curve; corners stay.
bool DynamicTopology::collapseTarget(uint32_t a, uint32_t b, uint32_t& keep, uint32_t& drop, glm::vec3& p) const {
    uint32_t oa[2], ob[2];
    const int fa = featureEdges(a, oa), fb = featureEdges(b, ob);
    const auto& pos = mesh_->positions;
    if (fa == 0 && fb == 0) {
        keep = std::min(a, b);
        drop = std::max(a, b);
        p = (pos[a] + pos[b]) * 0.5f;
        return true;
    }
    if (fa == 0) {
        keep = b, drop = a, p = pos[b];
        return true;
    }
    if (fb == 0) {
        keep = a, drop = b, p = pos[a];
        return true;
    }
    if (!featureEdge(a, b)) return false;
    if (fa == 2 && (oa[0] == b || oa[1] == b)) {
        keep = b, drop = a, p = pos[b];
        return true;
    }
    if (fb == 2 && (ob[0] == a || ob[1] == a)) {
        keep = a, drop = b, p = pos[a];
        return true;
    }
    return false;
}

bool DynamicTopology::canCollapse(uint32_t keep, uint32_t drop, const glm::vec3& p) const {
    const Mesh& m = *mesh_;
    uint32_t tris[4];
    const int n = edgeTriangles(keep, drop, tris);
    if (n < 1 || n > 2) return false;
    uint32_t opposite[2] = {kNone, kNone};
    for (int i = 0; i < n; ++i) {
        const glm::uvec3& tri = m.triangles[tris[i]];
        for (int c = 0; c < 3; ++c)
            if (tri[c] != keep && tri[c] != drop) opposite[i] = tri[c];
        if (opposite[i] == kNone) return false;
    }
    if (n == 2 && opposite[0] == opposite[1]) return false;
    std::vector<uint32_t> rk, rd;
    ring(keep, rk);
    ring(drop, rd);
    // Link condition: the ends share exactly the neighbours across the edge's triangles (else the surface
    // would pinch).
    size_t common = 0;
    for (uint32_t u : rk)
        if (std::binary_search(rd.begin(), rd.end(), u)) {
            if (u != opposite[0] && u != opposite[1]) return false;
            ++common;
        }
    if (common != size_t(n)) return false;
    // Nobody left with fewer than three neighbours (no flat double triangles).
    if (rk.size() + rd.size() - size_t(n) - 2 < 3) return false;
    std::vector<uint32_t> ro;
    for (int i = 0; i < n; ++i) {
        ring(opposite[i], ro);
        if (ro.size() <= 3) return false;
    }
    if (n == 1 && (rk.size() < 3 || rd.size() < 3)) return false;
    // No long edge, no triangle turned over or made degenerate or much worse.
    const auto& pos = m.positions;
    for (const std::vector<uint32_t>* r : {&rk, &rd})
        for (uint32_t u : *r)
            if (u != keep && u != drop) {
                const glm::vec3 d = pos[u] - p;
                if (glm::dot(d, d) >= splitLen2_) return false;
            }
    const float minArea = 1e-4f * target_ * target_;
    for (uint32_t v : {keep, drop})
        for (uint32_t t : m.vertexTriangles(v)) {
            if (t == tris[0] || (n == 2 && t == tris[1])) continue;
            const glm::uvec3& tri = m.triangles[t];
            glm::vec3 q[3], o[3];
            for (int c = 0; c < 3; ++c) {
                o[c] = pos[tri[c]];
                q[c] = tri[c] == keep || tri[c] == drop ? p : o[c];
            }
            const glm::vec3 nOld = glm::cross(o[1] - o[0], o[2] - o[0]);
            const glm::vec3 nNew = glm::cross(q[1] - q[0], q[2] - q[0]);
            const float lNew = glm::length(nNew), lOld = glm::length(nOld);
            if (lNew < minArea) return false;
            if (glm::dot(nOld, nNew) <= 0.3f * lOld * lNew) return false;
            const float qNew = quality(q[0], q[1], q[2]);
            if (qNew < 0.1f && qNew < quality(o[0], o[1], o[2])) return false;
        }
    return true;
}

void DynamicTopology::collapse(uint32_t keep, uint32_t drop, const glm::vec3& p) {
    Mesh& m = *mesh_;
    uint32_t tris[4];
    const int n = edgeTriangles(keep, drop, tris);
    for (int i = 0; i < n && i < 2; ++i) killTriangle(tris[i]);
    const IndexRing around = m.vertexTriangles(drop);
    std::vector<uint32_t> list(around.begin(), around.end());
    for (uint32_t t : list) {
        glm::uvec3 tri = m.triangles[t];
        for (int c = 0; c < 3; ++c)
            if (tri[c] == drop) tri[c] = keep;
        setTriangle(t, tri);
    }
    const bool midpoint = p != m.positions[keep];
    if (midpoint) interpolateVertex(keep, keep, drop, 0.5f, p);
    markVertex(keep);
    killVertex(drop);
    ++stats.collapses;
    ++ops_;
}

void DynamicTopology::collapsePass() {
    heap_.clear();
    uint32_t tris[4];
    for (uint32_t t : regionTris_) {
        if (t >= mesh_->triangleCount() || deadT_[t]) continue;
        const glm::uvec3 tri = mesh_->triangles[t];
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = tri[c], b = tri[(c + 1) % 3];
            if (a > b && edgeTriangles(a, b, tris) != 1) continue;
            const float l = len2(a, b);
            if (l < collapseLen2_ && inside(a, b)) heap_.push_back({-l, std::min(a, b), std::max(a, b)});
        }
    }
    auto less = [](const Edge& x, const Edge& y) {
        return x.len2 != y.len2 ? x.len2 < y.len2 : x.a != y.a ? x.a > y.a : x.b > y.b;
    };
    std::make_heap(heap_.begin(), heap_.end(), less);
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    struct Plan {
        uint32_t keep, drop;
        glm::vec3 p;
    };
    std::vector<Plan> plans;
    while (!heap_.empty() && ops_ < maxOps_) {
        std::pop_heap(heap_.begin(), heap_.end(), less);
        const Edge e = heap_.back();
        heap_.pop_back();
        if (deadV_[e.a] || deadV_[e.b]) continue;
        const int n = edgeTriangles(e.a, e.b, tris);
        if (n < 1 || n > 2 || len2(e.a, e.b) >= collapseLen2_) continue;
        if (locked(e.a) || locked(e.b)) continue;
        orbit(e.a, e.b, edges);
        if (edges.size() > 1 && orbitSharesTriangle(edges)) continue; // at the symmetry plane: leave it
        plans.clear();
        bool ok = true;
        for (const auto& [x, y] : edges) {
            Plan pl;
            if (locked(x) || locked(y) || !collapseTarget(x, y, pl.keep, pl.drop, pl.p) || !canCollapse(pl.keep, pl.drop, pl.p)) {
                ok = false;
                break;
            }
            plans.push_back(pl);
        }
        if (!ok) continue;
        if (orbitIncomplete_ && !mirrorMaps_.empty()) ++stats.unpaired;
        for (size_t i = 0; i < plans.size(); ++i) {
            const Plan& pl = plans[i];
            if (deadV_[pl.keep] || deadV_[pl.drop]) continue;
            if (i > 0 && !canCollapse(pl.keep, pl.drop, pl.p)) continue; // (the first changed its surroundings)
            collapse(pl.keep, pl.drop, pl.p);
            pushEdgesAround(pl.keep, false);
        }
    }
}

// Flipping a-b (between triangles a,b,c and b,a,d) to c-d: worth it when it brings the four valences closer
// to 6 (4 on a boundary) without folding or flattening the surface.
bool DynamicTopology::flipGain(uint32_t a, uint32_t b, uint32_t& c, uint32_t& d) const {
    const Mesh& m = *mesh_;
    uint32_t tris[4];
    if (edgeTriangles(a, b, tris) != 2 || featureEdge(a, b)) return false;
    c = d = kNone;
    for (int i = 0; i < 2; ++i) {
        const glm::uvec3& tri = m.triangles[tris[i]];
        int k = 0;
        while (k < 3 && tri[k] != a) ++k;
        if (k == 3) return false;
        const uint32_t next = tri[(k + 1) % 3], third = tri[(k + 2) % 3];
        if (next == b) c = third; // a -> b: the triangle a, b, c
        else d = next;            // b -> a: the triangle b, a, d
    }
    if (c == kNone || d == kNone || c == d) return false;
    uint32_t tmp[4];
    if (edgeTriangles(c, d, tmp) > 0) return false;
    std::vector<uint32_t> r;
    auto valence = [&](uint32_t v, int& target) {
        ring(v, r);
        target = boundaryVertex(v) ? 4 : 6;
        return int(r.size());
    };
    int ta, tb, tc, td;
    const int va = valence(a, ta), vb = valence(b, tb), vc = valence(c, tc), vd = valence(d, td);
    if (va <= 3 || vb <= 3) return false;
    auto sq = [](int x) { return x * x; };
    const int before = sq(va - ta) + sq(vb - tb) + sq(vc - tc) + sq(vd - td);
    const int after = sq(va - 1 - ta) + sq(vb - 1 - tb) + sq(vc + 1 - tc) + sq(vd + 1 - td);
    if (after >= before) return false;
    const auto& p = m.positions;
    const glm::vec3 n1 = glm::cross(p[b] - p[a], p[c] - p[a]), n2 = glm::cross(p[a] - p[b], p[d] - p[b]);
    const float l1 = glm::length(n1), l2 = glm::length(n2);
    if (glm::dot(n1, n2) <= 0.94f * l1 * l2) return false; // a crease: flipping would change the shape
    const glm::vec3 m1 = glm::cross(p[a] - p[c], p[d] - p[c]), m2 = glm::cross(p[b] - p[d], p[c] - p[d]);
    const float k1 = glm::length(m1), k2 = glm::length(m2);
    const float minArea = 1e-4f * target_ * target_;
    if (k1 < minArea || k2 < minArea) return false;
    const glm::vec3 nSum = n1 + n2;
    if (glm::dot(m1, nSum) <= 0.0f || glm::dot(m2, nSum) <= 0.0f || glm::dot(m1, m2) <= 0.9f * k1 * k2) return false;
    const float qOld = std::min(quality(p[a], p[b], p[c]), quality(p[b], p[a], p[d]));
    const float qNew = std::min(quality(p[c], p[a], p[d]), quality(p[d], p[b], p[c]));
    return qNew >= 0.5f * qOld;
}

void DynamicTopology::flip(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t tris[4];
    if (edgeTriangles(a, b, tris) != 2) return;
    const Mesh& m = *mesh_;
    uint32_t t1 = tris[0], t2 = tris[1];
    // t1 holds a -> b.
    {
        const glm::uvec3& tri = m.triangles[t1];
        int k = 0;
        while (k < 3 && tri[k] != a) ++k;
        if (tri[(k + 1) % 3] != b) std::swap(t1, t2);
    }
    setTriangle(t1, glm::uvec3(c, a, d));
    setTriangle(t2, glm::uvec3(d, b, c));
    ++stats.flips;
    ++ops_;
}

void DynamicTopology::flipPass() {
    // The interior edges of the triangles around changed vertices inside the brush, in a fixed order.
    std::vector<std::pair<uint32_t, uint32_t>> candidates;
    for (uint32_t v : changedV_) {
        if (deadV_[v]) continue;
        for (uint32_t t : mesh_->vertexTriangles(v)) {
            const glm::uvec3& tri = mesh_->triangles[t];
            for (int c = 0; c < 3; ++c) {
                const uint32_t a = tri[c], b = tri[(c + 1) % 3];
                if (a < b && inside(a, b)) candidates.emplace_back(a, b);
            }
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    struct Plan {
        uint32_t a, b, c, d;
    };
    std::vector<Plan> plans;
    for (const auto& [a0, b0] : candidates) {
        if (ops_ >= maxOps_) break;
        if (deadV_[a0] || deadV_[b0] || locked(a0) || locked(b0)) continue;
        uint32_t c, d;
        if (!flipGain(a0, b0, c, d)) continue;
        orbit(a0, b0, edges);
        if (edges.size() > 1 && orbitSharesTriangle(edges)) continue;
        plans.clear();
        bool ok = true;
        for (const auto& [x, y] : edges) {
            Plan pl{x, y, 0, 0};
            if (locked(x) || locked(y) || !flipGain(x, y, pl.c, pl.d)) {
                ok = false;
                break;
            }
            plans.push_back(pl);
        }
        if (!ok) continue;
        if (orbitIncomplete_ && !mirrorMaps_.empty()) ++stats.unpaired;
        for (const Plan& pl : plans) flip(pl.a, pl.b, pl.c, pl.d);
    }
}

// Moves each changed vertex inside the brush halfway to the centre of its neighbours, along the surface
// (free vertices only), which evens out the triangles the splits and collapses made.
void DynamicTopology::relaxPass() {
    Mesh& m = *mesh_;
    std::vector<std::pair<uint32_t, glm::vec3>> moves;
    std::vector<uint32_t> r;
    uint32_t other[2];
    const float r2 = radius_ * radius_;
    for (uint32_t v : changedV_) {
        if (deadV_[v] || locked(v)) continue;
        bool in = glm::dot(m.positions[v] - center_, m.positions[v] - center_) <= r2;
        for (const glm::vec3& s : mirrorSigns_) {
            const glm::vec3 d0 = m.positions[v] - center_ * s;
            in = in || glm::dot(d0, d0) <= r2;
        }
        if (!in) continue;
        if (featureEdges(v, other) != 0) continue;
        ring(v, r);
        if (r.size() < 3) continue;
        glm::vec3 avg(0.0f);
        for (uint32_t u : r) avg += m.positions[u];
        avg /= float(r.size());
        const glm::vec3 n = faceNormalAt(v);
        glm::vec3 d = avg - m.positions[v];
        d -= n * glm::dot(d, n);
        moves.emplace_back(v, d * 0.5f);
    }
    for (const auto& [v, d] : moves) {
        recordVertex(v);
        m.positions[v] += d;
        if (!m.base.empty()) m.base[v] += d;
    }
}

void DynamicTopology::gatherRegion() {
    bvh_->queryTriangles(*mesh_, center_, radius_, regionTris_);
    if (mirrorSigns_.empty()) return;
    // Every vertex of the triangles under each mirror image of the brush, to pair up.
    std::vector<uint32_t> verts, tris;
    auto addCorners = [&](const std::vector<uint32_t>& ts) {
        for (uint32_t t : ts)
            for (int c = 0; c < 3; ++c) verts.push_back(mesh_->triangles[t][c]);
    };
    addCorners(regionTris_);
    // (A margin: the mirror images of the corners found here must be among those found there, though
    // the two sides never match exactly.)
    const float reach = radius_ * 1.25f + target_ * 2.0f;
    for (const glm::vec3& s : mirrorSigns_) {
        bvh_->queryTriangles(*mesh_, center_ * s, reach, tris);
        addCorners(tris);
    }
    std::sort(verts.begin(), verts.end());
    verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
    matchMirrors(verts);
}

bool DynamicTopology::remesh(Mesh& mesh, BVH& bvh, const glm::vec3& center, float radius, float edgeLength,
                             std::span<const glm::vec3> mirrors) {
    const auto clock = std::chrono::steady_clock::now();
    moved_.clear();
    recordedNow_.clear();
    if (!dyntopoBlocker(mesh).empty() || !(edgeLength > 0.0f) || !(radius > 0.0f)) return false;
    if (mesh_ && mesh_ != &mesh) return false; // (another mesh mid-stroke)
    if (!mesh_) {
        if (!mesh.beginDynamicAdjacency()) return false;
        mesh_ = &mesh;
        bvh_ = &bvh;
        startVertices_ = uint32_t(mesh.vertexCount());
        startTriangles_ = uint32_t(mesh.triangleCount());
        undo_ = std::make_unique<TopologyUndo>();
        vertRec_.assign((mesh.vertexCount() + 63) / 64, 0);
        triRec_.assign((mesh.triangleCount() + 63) / 64, 0);
        deadV_.assign(mesh.vertexCount(), 0);
        deadT_.assign(mesh.triangleCount(), 0);
        changedVFlag_.assign(mesh.vertexCount(), 0);
        if (!mesh.uvs.empty()) { // a remeshed surface has no UVs: they come back with undo
            undo_->uvs = std::move(mesh.uvs);
            mesh.uvs.clear();
            mesh.topologyDirty = true;
        }
    }
    center_ = center;
    radius_ = radius;
    target_ = edgeLength;
    splitLen2_ = (edgeLength * 4.0f / 3.0f) * (edgeLength * 4.0f / 3.0f);
    collapseLen2_ = (edgeLength * 0.6f) * (edgeLength * 0.6f);
    // Mirror images are matched loosely: overlapping mirrored dabs near the plane don't move both sides
    // exactly alike, but vertices are rarely closer than half an edge.
    eps_ = std::max(edgeLength * 0.3f, 1e-7f);
    mirrorSigns_.assign(mirrors.size() > 1 ? mirrors.begin() + 1 : mirrors.end(), mirrors.end());
    mirrorMaps_.resize(mirrorSigns_.size());
    for (auto& map : mirrorMaps_) map.clear();
    ops_ = 0;
    maxOps_ = 6000; // (bounds a dab's time: a big brush over fine triangles coarsens them over several dabs)

    gatherRegion();
    if (regionTris_.empty()) return false;
    splitPass();
    bvh.refitChanged(mesh);
    bvh.queryTriangles(mesh, center_, radius_, regionTris_);
    collapsePass();
    flipPass();
    relaxPass();
    const bool changed = !changedV_.empty() || !changedT_.empty();
    compact();

    // Rings and normals of everything that changed, the BVH's boxes, and what the renderer must patch.
    const uint32_t nv = uint32_t(mesh.vertexCount());
    for (uint32_t v : changedV_)
        if (v < changedVFlag_.size()) changedVFlag_[v] = 0;
    std::vector<uint32_t> verts;
    verts.reserve(changedV_.size());
    for (uint32_t v : changedV_) {
        if (v < nv) {
            verts.push_back(v);
            continue;
        }
        for (const auto& [from, to] : moved_)
            if (from == v) verts.push_back(to);
    }
    std::sort(verts.begin(), verts.end());
    verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
    changedV_.clear();
    changedVFlag_.resize(nv, 0);
    for (uint32_t v : verts) {
        ring(v, scratch2_);
        mesh.setNeighbors(v, scratch2_);
        mesh.markDirty(v);
        for (uint32_t t : mesh.vertexTriangles(v)) bvh.touchTriangle(t); // (moved vertices: relaxed, collapsed)
    }
    mesh.updateNormals(verts);
    bvh.refitChanged(mesh);
    mesh.changedTriangles.insert(mesh.changedTriangles.end(), changedT_.begin(), changedT_.end());
    changedT_.clear();
    lastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count();
    return changed;
}

std::unique_ptr<TopologyUndo> DynamicTopology::finish(Mesh& mesh, BVH& bvh) {
    if (!mesh_) return nullptr;
    mesh.endDynamicAdjacency();
    if (bvh.degraded()) bvh.build(mesh);
    std::unique_ptr<TopologyUndo> undo = std::move(undo_);
    undo->vertexCount = startVertices_;
    undo->triangleCount = startTriangles_;
    mesh_ = nullptr;
    bvh_ = nullptr;
    std::vector<uint64_t>().swap(vertRec_);
    std::vector<uint64_t>().swap(triRec_);
    std::vector<uint8_t>().swap(deadV_);
    std::vector<uint8_t>().swap(deadT_);
    std::vector<uint8_t>().swap(changedVFlag_);
    for (auto& map : mirrorMaps_) map.clear();
    if (undo->vertexSlots.empty() && undo->triangleSlots.empty() && undo->uvs.empty()) return nullptr;
    return undo;
}

} // namespace sf
