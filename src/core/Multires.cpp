// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Multires.h"

#include "core/BVH.h"
#include "core/Document.h"
#include "core/PaintLayer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <numbers>
#include <optional>
#include <thread>
#include <unordered_map>

namespace sf {

namespace {

constexpr uint32_t kNone = SubdivTopology::kNone;

// Runs fn(begin, end) over [0, n) on several threads for big meshes.
template <class Fn>
void parallelRange(size_t n, Fn fn) {
    const size_t threads = n < 100000 ? 1 : std::clamp<size_t>(std::thread::hardware_concurrency(), 1, 16);
    if (threads <= 1) {
        if (n) fn(size_t(0), n);
        return;
    }
    std::vector<std::thread> pool;
    const size_t chunk = (n + threads - 1) / threads;
    for (size_t t = 0; t < threads; ++t) {
        const size_t b = t * chunk, e = std::min(n, b + chunk);
        if (b < e) pool.emplace_back([=] { fn(b, e); });
    }
    for (auto& th : pool) th.join();
}

template <class T>
bool sameArray(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

// The level's data without topology, normals, texture or stack: what a level stores. (Add new Mesh attributes
// here too, so they follow the levels.)
// Copies the level arrays of `m` (all but the triangles) over `out`.
void assignLevelArrays(Mesh& out, const Mesh& m) {
    out.positions = m.positions;
    out.mask = m.mask;
    out.colors = m.colors;
    out.polygroups = m.polygroups;
    out.hidden = m.hidden;
    out.uvs = m.uvs;
    out.quadPartners = m.quadPartners;
    out.base = m.base;
    out.layers = m.layers;
    out.paintBase = m.paintBase;
    out.paintLayers = m.paintLayers;
    out.activePaintLayer = m.activePaintLayer;
}
Mesh leanCopy(const Mesh& m) {
    Mesh out;
    out.triangles = m.triangles;
    assignLevelArrays(out, m);
    return out;
}
std::shared_ptr<Mesh> leanShared(const Mesh& m) { return std::make_shared<Mesh>(leanCopy(m)); }

// The shape channel subdivision and details work on: the positions, or the layer base while sculpt layers exist.
std::vector<glm::vec3>& baseChannel(Mesh& m) { return m.layers.empty() ? m.positions : m.base; }
const std::vector<glm::vec3>& baseChannel(const Mesh& m) { return m.layers.empty() ? m.positions : m.base; }

void evaluateLayers(Mesh& m) {
    if (m.layers.empty()) return;
    parallelRange(m.positions.size(), [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            glm::vec3 p = m.base[v];
            for (const auto& l : m.layers) p += l.weight * l.delta[v];
            m.positions[v] = p;
        }
    });
}

// --- Copy-on-write access ---
MultiresStack& mutableStack(Mesh& m) {
    if (m.multires.use_count() > 1) m.multires = std::make_shared<MultiresStack>(*m.multires);
    return *m.multires;
}
Mesh& mutableData(SubdivLevel& l) {
    if (l.data.use_count() > 1) l.data = std::make_shared<Mesh>(*l.data);
    return *l.data;
}
std::vector<glm::vec3>& mutableArray(std::shared_ptr<std::vector<glm::vec3>>& a, size_t size) {
    if (!a) a = std::make_shared<std::vector<glm::vec3>>(size, glm::vec3(0.0f));
    else if (a.use_count() > 1) a = std::make_shared<std::vector<glm::vec3>>(*a);
    if (a->size() != size) a->resize(size, glm::vec3(0.0f));
    return *a;
}

// --- Topology ---

uint32_t polySize(const SubdivTopology& tp, size_t p) { return tp.polyOffsets[p + 1] - tp.polyOffsets[p]; }

// Polygons, edges and vertex -> edge lists of `coarse` with the given quad pairing (empty = all triangles).
// Pairs that aren't valid quads are treated as triangles (and dropped from tp.pairing).
std::shared_ptr<SubdivTopology> buildCoarse(const Mesh& coarse, std::vector<uint32_t> pairing, bool cc, bool smooth) {
    auto tp = std::make_shared<SubdivTopology>();
    tp->catmullClark = cc;
    tp->smooth = smooth;
    const uint32_t nv = uint32_t(coarse.vertexCount()), nt = uint32_t(coarse.triangleCount());
    tp->coarseVertices = nv;
    if (pairing.size() != nt) pairing.clear();
    bool anyPair = false;
    for (uint32_t t = 0; t < pairing.size(); ++t) {
        const uint32_t u = pairing[t];
        if (u == kNone) continue;
        if (u >= nt || pairing[u] != t || coarse.quadDiagonal(t, u) < 0) pairing[t] = kNone;
        else anyPair = true;
    }
    for (uint32_t t = 0; t < pairing.size(); ++t)
        if (pairing[t] != kNone && pairing[pairing[t]] != t) pairing[t] = kNone;
    if (!anyPair) pairing.clear();

    tp->polyOffsets.reserve(nt + 1);
    tp->polyOffsets.push_back(0);
    tp->polyVerts.reserve(nt * 3);
    tp->polyCorner.reserve(nt * 3);
    tp->polyTris.reserve(nt);
    for (uint32_t t = 0; t < nt; ++t) {
        const uint32_t u = pairing.empty() ? kNone : pairing[t];
        const glm::uvec3& a = coarse.triangles[t];
        if (u == kNone) {
            for (uint32_t c = 0; c < 3; ++c) {
                tp->polyVerts.push_back(a[c]);
                tp->polyCorner.push_back(t * 3 + c);
            }
            tp->polyTris.push_back({t, kNone});
        } else {
            if (u < t) continue;
            const int e = coarse.quadDiagonal(t, u);
            const glm::uvec3& b = coarse.triangles[u];
            uint32_t far = 0;
            for (uint32_t k = 0; k < 3; ++k)
                if (b[k] != a[0] && b[k] != a[1] && b[k] != a[2]) far = k;
            for (int c : {(e + 1) % 3, (e + 2) % 3, e}) {
                tp->polyVerts.push_back(a[c]);
                tp->polyCorner.push_back(t * 3 + uint32_t(c));
            }
            tp->polyVerts.push_back(b[far]);
            tp->polyCorner.push_back(u * 3 + far);
            tp->polyTris.push_back({t, u});
        }
        tp->polyOffsets.push_back(uint32_t(tp->polyVerts.size()));
    }
    tp->pairing = std::move(pairing);

    // Edges: half-edges bucketed by their lower vertex, then matched within each (small) bucket.
    const size_t np = tp->polygonCount();
    std::vector<uint32_t> bucket(size_t(nv) + 1, 0);
    for (size_t p = 0; p < np; ++p)
        for (uint32_t i = tp->polyOffsets[p]; i < tp->polyOffsets[p + 1]; ++i) {
            const uint32_t a = tp->polyVerts[i], b = tp->polyVerts[i + 1 == tp->polyOffsets[p + 1] ? tp->polyOffsets[p] : i + 1];
            ++bucket[std::min(a, b) + 1];
        }
    for (size_t v = 0; v < nv; ++v) bucket[v + 1] += bucket[v];
    struct Half {
        uint32_t other, slot; // slot = index into polyVerts (the side starting there)
    };
    std::vector<Half> halves(bucket[nv]);
    {
        std::vector<uint32_t> cursor(bucket.begin(), bucket.end() - 1);
        for (size_t p = 0; p < np; ++p)
            for (uint32_t i = tp->polyOffsets[p]; i < tp->polyOffsets[p + 1]; ++i) {
                const uint32_t a = tp->polyVerts[i], b = tp->polyVerts[i + 1 == tp->polyOffsets[p + 1] ? tp->polyOffsets[p] : i + 1];
                halves[cursor[std::min(a, b)]++] = {std::max(a, b), i};
            }
    }
    std::vector<uint32_t> slotPoly(tp->polyVerts.size());
    for (size_t p = 0; p < np; ++p)
        for (uint32_t i = tp->polyOffsets[p]; i < tp->polyOffsets[p + 1]; ++i) slotPoly[i] = uint32_t(p);
    tp->polyEdges.assign(tp->polyVerts.size(), kNone);
    tp->edges.reserve(halves.size() / 2 + 16);
    tp->edgeFaces.reserve(halves.size() / 2 + 16);
    std::vector<uint8_t> faceCount;
    faceCount.reserve(halves.size() / 2 + 16);
    for (uint32_t v = 0; v < nv; ++v) {
        const uint32_t b0 = bucket[v], b1 = bucket[v + 1];
        for (uint32_t i = b0; i < b1; ++i) {
            uint32_t edge = kNone;
            for (uint32_t j = b0; j < i; ++j)
                if (halves[j].other == halves[i].other) {
                    edge = tp->polyEdges[halves[j].slot];
                    break;
                }
            const uint32_t poly = slotPoly[halves[i].slot];
            if (edge == kNone) {
                edge = uint32_t(tp->edges.size());
                tp->edges.push_back({v, halves[i].other});
                tp->edgeFaces.push_back({poly, kNone});
                faceCount.push_back(1);
            } else if (faceCount[edge]++ == 1) {
                tp->edgeFaces[edge].y = poly;
            } else {
                tp->edgeFaces[edge].y = kNone; // non-manifold: subdivided like a boundary (kept sharp)
            }
            tp->polyEdges[halves[i].slot] = edge;
        }
    }
    for (size_t e = 0; e < faceCount.size(); ++e)
        if (faceCount[e] > 2) tp->edgeFaces[e].y = kNone;

    tp->vertEdgeOffsets.assign(size_t(nv) + 1, 0);
    for (const auto& e : tp->edges) {
        ++tp->vertEdgeOffsets[e.x + 1];
        ++tp->vertEdgeOffsets[e.y + 1];
    }
    for (size_t v = 0; v < nv; ++v) tp->vertEdgeOffsets[v + 1] += tp->vertEdgeOffsets[v];
    tp->vertEdges.resize(tp->vertEdgeOffsets[nv]);
    {
        std::vector<uint32_t> cursor(tp->vertEdgeOffsets.begin(), tp->vertEdgeOffsets.end() - 1);
        for (uint32_t e = 0; e < tp->edges.size(); ++e) {
            tp->vertEdges[cursor[tp->edges[e].x]++] = e;
            tp->vertEdges[cursor[tp->edges[e].y]++] = e;
        }
    }
    tp->fineVertices = nv + uint32_t(tp->edges.size()) + (cc ? uint32_t(np) : 0u);
    return tp;
}

// The coarse triangle of polygon `p` that holds side `side`.
uint32_t sideTriangle(const SubdivTopology& tp, const Mesh& coarse, uint32_t p, uint32_t side) {
    const glm::uvec2 tris = tp.polyTris[p];
    if (tris.y == kNone) return tris.x;
    const uint32_t o = tp.polyOffsets[p], n = polySize(tp, p);
    const uint32_t a = tp.polyVerts[o + side], b = tp.polyVerts[o + (side + 1) % n];
    const glm::uvec3& t = coarse.triangles[tris.x];
    auto has = [&](uint32_t v) { return t.x == v || t.y == v || t.z == v; };
    return has(a) && has(b) ? tris.x : tris.y;
}

void buildFrameRefs(SubdivTopology& tp, const std::vector<glm::uvec3>& fine) {
    tp.frameRef.assign(tp.fineVertices, kNone);
    for (const auto& t : fine)
        for (int c = 0; c < 3; ++c)
            if (tp.frameRef[t[c]] == kNone) tp.frameRef[t[c]] = t[(c + 1) % 3];
}

// Fine triangles (with quad pairs) of a freshly divided level, and their parents / corner codes.
void buildDividedFaces(SubdivTopology& tp, const Mesh& coarse, Mesh& fine) {
    const uint32_t nv = tp.coarseVertices, ne = uint32_t(tp.edges.size());
    const size_t np = tp.polygonCount();
    tp.vertexChild.resize(nv);
    for (uint32_t v = 0; v < nv; ++v) tp.vertexChild[v] = v;
    tp.edgeChild.resize(ne);
    for (uint32_t e = 0; e < ne; ++e) tp.edgeChild[e] = nv + e;
    if (tp.catmullClark) {
        tp.faceChild.resize(np);
        for (uint32_t p = 0; p < np; ++p) tp.faceChild[p] = nv + ne + p;
    }
    size_t fineCount = 0;
    for (size_t p = 0; p < np; ++p) fineCount += tp.catmullClark ? 2 * polySize(tp, p) : 4;
    fine.triangles.clear();
    fine.triangles.reserve(fineCount);
    tp.fineParentPoly.reserve(fineCount);
    tp.fineParentTri.reserve(fineCount);
    tp.fineCode.reserve(fineCount * 3);
    if (tp.catmullClark) fine.quadPartners.assign(fineCount, Mesh::kNoQuad);
    auto emit = [&](uint32_t p, uint32_t tri, glm::uvec3 v, uint8_t c0, uint8_t c1, uint8_t c2) {
        fine.triangles.push_back(v);
        tp.fineParentPoly.push_back(p);
        tp.fineParentTri.push_back(tri);
        tp.fineCode.insert(tp.fineCode.end(), {c0, c1, c2});
    };
    for (uint32_t p = 0; p < np; ++p) {
        const uint32_t o = tp.polyOffsets[p], n = polySize(tp, p);
        auto corner = [&](uint32_t i) { return tp.polyVerts[o + i % n]; };
        auto edgePt = [&](uint32_t i) { return nv + tp.polyEdges[o + i % n]; };
        if (tp.catmullClark) {
            const uint32_t f = tp.faceChild[p];
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t tri = sideTriangle(tp, coarse, p, i), prev = (i + n - 1) % n;
                const uint32_t a = uint32_t(fine.triangles.size());
                emit(p, tri, {corner(i), edgePt(i), f}, uint8_t(i), uint8_t(4 + i), 8);
                emit(p, tri, {corner(i), f, edgePt(prev)}, uint8_t(i), 8, uint8_t(4 + prev));
                fine.quadPartners[a] = a + 1;
                fine.quadPartners[a + 1] = a;
            }
        } else {
            const uint32_t tri = tp.polyTris[p].x;
            emit(p, tri, {corner(0), edgePt(0), edgePt(2)}, 0, 4, 6);
            emit(p, tri, {corner(1), edgePt(1), edgePt(0)}, 1, 5, 4);
            emit(p, tri, {corner(2), edgePt(2), edgePt(1)}, 2, 6, 5);
            emit(p, tri, {edgePt(0), edgePt(1), edgePt(2)}, 4, 5, 6);
        }
    }
    buildFrameRefs(tp, fine.triangles);
}

// Parents and corner codes of arbitrary fine triangles from the child maps (reconstructed or loaded levels).
bool buildFineMetadata(SubdivTopology& tp, const Mesh& coarse, const std::vector<glm::uvec3>& fine, std::string& error) {
    const uint32_t nf = tp.fineVertices;
    std::vector<uint8_t> kind(nf, 255);
    std::vector<uint32_t> id(nf, kNone);
    auto mark = [&](const std::vector<uint32_t>& child, uint8_t k) {
        for (uint32_t i = 0; i < child.size(); ++i) {
            if (child[i] >= nf || kind[child[i]] != 255) return false;
            kind[child[i]] = k;
            id[child[i]] = i;
        }
        return true;
    };
    if (tp.vertexChild.size() != tp.coarseVertices || tp.edgeChild.size() != tp.edges.size() ||
        tp.faceChild.size() != (tp.catmullClark ? tp.polygonCount() : 0) || !mark(tp.vertexChild, 0) ||
        !mark(tp.edgeChild, 1) || !mark(tp.faceChild, 2)) {
        error = "the subdivision levels don't match";
        return false;
    }
    for (uint32_t v = 0; v < nf; ++v)
        if (kind[v] == 255) {
            error = "the subdivision levels don't match";
            return false;
        }
    const size_t nt = fine.size();
    tp.fineParentPoly.assign(nt, kNone);
    tp.fineParentTri.assign(nt, kNone);
    tp.fineCode.assign(nt * 3, 0);
    std::atomic<bool> ok = true;
    parallelRange(nt, [&](size_t b, size_t e) {
        for (size_t f = b; f < e && ok.load(std::memory_order_relaxed); ++f) {
            const glm::uvec3& t = fine[f];
            uint32_t cand[3] = {kNone, kNone, kNone};
            for (int c = 0; c < 3 && cand[0] == kNone; ++c) {
                if (t[c] >= nf) break;
                if (kind[t[c]] == 1) {
                    cand[0] = tp.edgeFaces[id[t[c]]].x;
                    cand[1] = tp.edgeFaces[id[t[c]]].y;
                    // A non-manifold edge lists only one polygon here; also try the others it borders.
                } else if (kind[t[c]] == 2) {
                    cand[0] = id[t[c]];
                }
            }
            bool found = false;
            for (uint32_t p : cand) {
                if (p == kNone) continue;
                const uint32_t o = tp.polyOffsets[p], n = polySize(tp, p);
                uint8_t codes[3];
                bool all = true;
                for (int c = 0; c < 3 && all; ++c) {
                    const uint32_t v = t[c];
                    all = false;
                    if (kind[v] == 0) {
                        for (uint32_t i = 0; i < n; ++i)
                            if (tp.polyVerts[o + i] == id[v]) codes[c] = uint8_t(i), all = true;
                    } else if (kind[v] == 1) {
                        for (uint32_t i = 0; i < n; ++i)
                            if (tp.polyEdges[o + i] == id[v]) codes[c] = uint8_t(4 + i), all = true;
                    } else if (id[v] == p) {
                        codes[c] = 8, all = true;
                    }
                }
                if (!all) continue;
                int side = -1, e1 = -1, e2 = -1;
                for (int c = 0; c < 3; ++c) {
                    if (codes[c] < 4 && side < 0) side = codes[c];
                    if (codes[c] >= 4 && codes[c] < 8) (e1 < 0 ? e1 : e2) = codes[c] - 4;
                }
                if (side < 0 && e1 >= 0) side = e2 >= 0 && (uint32_t(e1) + 1) % n == uint32_t(e2) ? e2 : e1;
                tp.fineParentPoly[f] = p;
                tp.fineParentTri[f] = sideTriangle(tp, coarse, p, uint32_t(std::max(side, 0)));
                for (int c = 0; c < 3; ++c) tp.fineCode[f * 3 + size_t(c)] = codes[c];
                found = true;
                break;
            }
            if (!found) ok = false;
        }
    });
    if (!ok) {
        error = "the mesh is not a regular subdivision of the level below";
        return false;
    }
    buildFrameRefs(tp, fine);
    return true;
}

// --- Stencils ---

// The subdivided field: the coarse values `P` through the level's stencils (Catmull-Clark / Loop when
// `smooth`, else linear: edge midpoints and polygon centres).
template <class T>
std::vector<T> subdivideField(const SubdivTopology& tp, bool smooth, const std::vector<T>& P) {
    std::vector<T> out(tp.fineVertices);
    const size_t np = tp.polygonCount(), ne = tp.edges.size();
    std::vector<T> fp;
    if (tp.catmullClark) {
        fp.resize(np);
        parallelRange(np, [&](size_t b, size_t e) {
            for (size_t p = b; p < e; ++p) {
                T sum = T(0);
                const uint32_t o = tp.polyOffsets[p], n = polySize(tp, p);
                for (uint32_t i = 0; i < n; ++i) sum += P[tp.polyVerts[o + i]];
                fp[p] = sum / float(n);
                out[tp.faceChild[p]] = fp[p];
            }
        });
    }
    auto opposite = [&](uint32_t p, uint32_t a, uint32_t b) {
        const uint32_t o = tp.polyOffsets[p];
        for (uint32_t i = 0; i < 3; ++i)
            if (tp.polyVerts[o + i] != a && tp.polyVerts[o + i] != b) return tp.polyVerts[o + i];
        return a;
    };
    parallelRange(ne, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const uint32_t va = tp.edges[i].x, vb = tp.edges[i].y;
            const glm::uvec2 f = tp.edgeFaces[i];
            T v;
            if (!smooth || f.y == kNone) v = (P[va] + P[vb]) * 0.5f;
            else if (tp.catmullClark) v = (P[va] + P[vb] + fp[f.x] + fp[f.y]) * 0.25f;
            else v = (P[va] + P[vb]) * 0.375f + (P[opposite(f.x, va, vb)] + P[opposite(f.y, va, vb)]) * 0.125f;
            out[tp.edgeChild[i]] = v;
        }
    });
    parallelRange(tp.coarseVertices, [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            const T& pv = P[v];
            T& o = out[tp.vertexChild[v]];
            const uint32_t e0 = tp.vertEdgeOffsets[v], e1 = tp.vertEdgeOffsets[v + 1], n = e1 - e0;
            if (!smooth || n == 0) {
                o = pv;
                continue;
            }
            uint32_t boundary = 0;
            T bsum = T(0), ring = T(0), faces = T(0);
            for (uint32_t k = e0; k < e1; ++k) {
                const uint32_t ei = tp.vertEdges[k];
                const uint32_t u = tp.edges[ei].x == v ? tp.edges[ei].y : tp.edges[ei].x;
                ring += P[u];
                if (tp.edgeFaces[ei].y == kNone) {
                    ++boundary;
                    bsum += P[u];
                } else if (tp.catmullClark) {
                    faces += (fp[tp.edgeFaces[ei].x] + fp[tp.edgeFaces[ei].y]) * 0.5f;
                }
            }
            if (boundary) {
                o = boundary == 2 ? pv * 0.75f + bsum * 0.125f : pv; // crease rule; corners stay put
            } else if (n < 3) {
                o = pv;
            } else if (tp.catmullClark) {
                const float fn = float(n);
                const T R = (pv * fn + ring) * (0.5f / fn), F = faces / fn;
                o = (F + R * 2.0f + pv * (fn - 3.0f)) / fn;
            } else {
                const float fn = float(n);
                const float c = 0.375f + 0.25f * std::cos(2.0f * std::numbers::pi_v<float> / fn);
                const float beta = (0.625f - c * c) / fn;
                o = pv * (1.0f - fn * beta) + ring * beta;
            }
        }
    });
    return out;
}

// Vertex normals of `Q` on the fine triangles (area weighted).
std::vector<glm::vec3> fineNormals(const std::vector<glm::uvec3>& tris, const std::vector<glm::vec3>& Q) {
    std::vector<glm::vec3> n(Q.size(), glm::vec3(0.0f));
    for (const auto& t : tris) {
        const glm::vec3 f = glm::cross(Q[t.y] - Q[t.x], Q[t.z] - Q[t.x]);
        n[t.x] += f;
        n[t.y] += f;
        n[t.z] += f;
    }
    return n;
}

// The local frame of fine vertex v: normal from `normal` (unnormalised sum), tangent towards its reference
// neighbour. Degenerate spots fall back to world axes.
inline void frameAt(glm::vec3 n, const glm::vec3& q, const glm::vec3& qref, glm::vec3& t, glm::vec3& b, glm::vec3& nn) {
    const float len = glm::length(n);
    if (!(len > 1e-30f)) {
        t = {1, 0, 0};
        b = {0, 1, 0};
        nn = {0, 0, 1};
        return;
    }
    n /= len;
    glm::vec3 d = qref - q;
    d -= n * glm::dot(n, d);
    const float dl = glm::length(d);
    if (dl > 1e-30f) d /= dl;
    else d = glm::normalize(std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0)) : glm::cross(n, glm::vec3(0, 1, 0)));
    t = d;
    b = glm::cross(n, d);
    nn = n;
}

// P[k] = Q + Frame(Q) * D, or D = Frame(Q)^-1 (P[k] - Q), with Q = the subdivided coarse shape.
void applyDetail(const SubdivTopology& tp, const std::vector<glm::uvec3>& tris, const std::vector<glm::vec3>& Q,
                 const std::vector<glm::vec3>& D, std::vector<glm::vec3>& out) {
    const std::vector<glm::vec3> n = fineNormals(tris, Q);
    out.resize(Q.size());
    parallelRange(Q.size(), [&](size_t b0, size_t e0) {
        for (size_t v = b0; v < e0; ++v) {
            glm::vec3 t, b, nn;
            const uint32_t r = tp.frameRef[v];
            frameAt(n[v], Q[v], r == kNone ? Q[v] : Q[r], t, b, nn);
            out[v] = Q[v] + t * D[v].x + b * D[v].y + nn * D[v].z;
        }
    });
}
void fitDetail(const SubdivTopology& tp, const std::vector<glm::uvec3>& tris, const std::vector<glm::vec3>& Q,
               const std::vector<glm::vec3>& P, std::vector<glm::vec3>& D) {
    const std::vector<glm::vec3> n = fineNormals(tris, Q);
    D.resize(Q.size());
    parallelRange(Q.size(), [&](size_t b0, size_t e0) {
        for (size_t v = b0; v < e0; ++v) {
            glm::vec3 t, b, nn;
            const uint32_t r = tp.frameRef[v];
            frameAt(n[v], Q[v], r == kNone ? Q[v] : Q[r], t, b, nn);
            const glm::vec3 d = P[v] - Q[v];
            D[v] = {glm::dot(d, t), glm::dot(d, b), glm::dot(d, nn)};
        }
    });
}

// --- Moving attributes between neighbouring levels. `flags` (null = every element) say which elements
// changed; the ones written are flagged in `outFlags`. ---

template <class T>
void restrictVertices(const SubdivTopology& tp, const std::vector<T>& fine, std::vector<T>& coarse, T fill,
                      const std::vector<uint8_t>* flags, std::vector<uint8_t>& outFlags) {
    if (coarse.size() != tp.coarseVertices) coarse.assign(tp.coarseVertices, fill);
    outFlags.assign(tp.coarseVertices, 0);
    for (uint32_t v = 0; v < tp.coarseVertices; ++v) {
        const uint32_t f = tp.vertexChild[v];
        if (flags && !(*flags)[f]) continue;
        coarse[v] = fine[f];
        outFlags[v] = 1;
    }
}

template <class T>
void prolongVertices(const SubdivTopology& tp, const std::vector<T>& coarse, std::vector<T>& fine, T fill,
                     const std::vector<uint8_t>* flags, std::vector<uint8_t>& outFlags) {
    if (fine.size() != tp.fineVertices) {
        fine.assign(tp.fineVertices, fill);
        flags = nullptr;
    }
    outFlags.assign(tp.fineVertices, 0);
    auto on = [&](uint32_t v) { return !flags || (*flags)[v]; };
    for (uint32_t v = 0; v < tp.coarseVertices; ++v)
        if (on(v)) {
            fine[tp.vertexChild[v]] = coarse[v];
            outFlags[tp.vertexChild[v]] = 1;
        }
    for (size_t e = 0; e < tp.edges.size(); ++e) {
        const uint32_t a = tp.edges[e].x, b = tp.edges[e].y;
        if (!on(a) && !on(b)) continue;
        fine[tp.edgeChild[e]] = (coarse[a] + coarse[b]) * 0.5f;
        outFlags[tp.edgeChild[e]] = 1;
    }
    for (size_t p = 0; p < tp.faceChild.size(); ++p) {
        const uint32_t o = tp.polyOffsets[p], n = polySize(tp, p);
        bool any = false;
        T sum = T(0);
        for (uint32_t i = 0; i < n; ++i) {
            any = any || on(tp.polyVerts[o + i]);
            sum += coarse[tp.polyVerts[o + i]];
        }
        if (!any) continue;
        fine[tp.faceChild[p]] = sum / float(n);
        outFlags[tp.faceChild[p]] = 1;
    }
}

// Paint layer colour and coverage together: midpoints average premultiplied, so unpainted (white,
// transparent) neighbours don't pale the edge (as subdividePaintLayers does).
void prolongPaintLayer(const SubdivTopology& tp, const PaintLayer& coarse, PaintLayer& fine,
                       const std::vector<uint8_t>* flags) {
    std::vector<uint8_t> dummy;
    std::vector<glm::vec3> premul(coarse.color.size());
    for (size_t v = 0; v < premul.size(); ++v) premul[v] = coarse.color[v] * coarse.alpha[v];
    std::vector<glm::vec3> fineColor = fine.color;
    std::vector<uint8_t> outFlags;
    prolongVertices(tp, coarse.alpha, fine.alpha, 0.0f, flags, outFlags);
    prolongVertices(tp, premul, fineColor, glm::vec3(1.0f), flags, dummy);
    if (fine.color.size() != tp.fineVertices) fine.color.assign(tp.fineVertices, glm::vec3(1.0f));
    for (uint32_t v = 0; v < tp.fineVertices; ++v)
        if (outFlags[v]) fine.color[v] = fine.alpha[v] > 0.0f ? fineColor[v] / fine.alpha[v] : glm::vec3(1.0f);
    for (uint32_t v = 0; v < tp.coarseVertices; ++v)
        if (!flags || (*flags)[v]) fine.color[tp.vertexChild[v]] = coarse.color[v]; // corners keep the exact colour
}

template <class T>
void restrictFaces(const SubdivTopology& tp, const std::vector<T>& fine, std::vector<T>& coarse, size_t coarseCount,
                   T fill, const std::vector<uint8_t>* flags, std::vector<uint8_t>& outFlags) {
    if (coarse.size() != coarseCount) coarse.assign(coarseCount, fill);
    outFlags.assign(coarseCount, 0);
    for (size_t f = 0; f < fine.size(); ++f) {
        if (flags && !(*flags)[f]) continue;
        // The child at a coarse corner speaks for its parent (centre children only when no corner child did).
        const uint32_t t = tp.fineParentTri[f];
        const bool cornerChild = tp.fineCode[f * 3] < 4 || tp.fineCode[f * 3 + 1] < 4 || tp.fineCode[f * 3 + 2] < 4;
        if (outFlags[t] == 2 && !cornerChild) continue;
        coarse[t] = fine[f];
        outFlags[t] = cornerChild ? 2 : 1;
    }
    for (auto& f : outFlags) f = f ? 1 : 0;
}

template <class T>
void prolongFaces(const SubdivTopology& tp, const std::vector<T>& coarse, std::vector<T>& fine, T fill,
                  const std::vector<uint8_t>* flags, std::vector<uint8_t>& outFlags) {
    const size_t nf = tp.fineParentTri.size();
    if (fine.size() != nf) {
        fine.assign(nf, fill);
        flags = nullptr;
    }
    outFlags.assign(nf, 0);
    for (size_t f = 0; f < nf; ++f) {
        const uint32_t t = tp.fineParentTri[f];
        if (flags && !(*flags)[t]) continue;
        fine[f] = coarse[t];
        outFlags[f] = 1;
    }
}

// UVs per triangle corner; `flags` are per triangle.
void restrictUvs(const SubdivTopology& tp, const Mesh& coarseMesh, const std::vector<glm::vec2>& fine,
                 std::vector<glm::vec2>& coarse, const std::vector<uint8_t>* flags, std::vector<uint8_t>& outFlags) {
    const size_t nt = coarseMesh.triangleCount();
    if (coarse.size() != nt * 3) coarse.assign(nt * 3, glm::vec2(0.0f));
    outFlags.assign(nt, 0);
    for (size_t f = 0; f < tp.fineParentPoly.size(); ++f) {
        if (flags && !(*flags)[f]) continue;
        const uint32_t p = tp.fineParentPoly[f], o = tp.polyOffsets[p];
        for (int c = 0; c < 3; ++c) {
            const uint8_t code = tp.fineCode[f * 3 + size_t(c)];
            if (code >= 4) continue;
            const uint32_t cv = tp.polyVerts[o + code];
            for (uint32_t t : {tp.polyTris[p].x, tp.polyTris[p].y}) {
                if (t == kNone) continue;
                for (uint32_t s = 0; s < 3; ++s)
                    if (coarseMesh.triangles[t][s] == cv) {
                        coarse[t * 3 + s] = fine[f * 3 + size_t(c)];
                        outFlags[t] = 1;
                    }
            }
        }
    }
}

void prolongUvs(const SubdivTopology& tp, const std::vector<glm::vec2>& coarse, std::vector<glm::vec2>& fine,
                const std::vector<uint8_t>* flags, std::vector<uint8_t>& outFlags) {
    const size_t nf = tp.fineParentPoly.size();
    if (fine.size() != nf * 3) {
        fine.assign(nf * 3, glm::vec2(0.0f));
        flags = nullptr;
    }
    outFlags.assign(nf, 0);
    for (size_t f = 0; f < nf; ++f) {
        const uint32_t p = tp.fineParentPoly[f];
        if (flags && !(*flags)[tp.polyTris[p].x] && (tp.polyTris[p].y == kNone || !(*flags)[tp.polyTris[p].y])) continue;
        const uint32_t o = tp.polyOffsets[p], n = polySize(tp, p);
        auto cu = [&](uint32_t i) { return coarse[tp.polyCorner[o + i % n]]; };
        for (int c = 0; c < 3; ++c) {
            const uint8_t code = tp.fineCode[f * 3 + size_t(c)];
            glm::vec2 uv;
            if (code < 4) uv = cu(code);
            else if (code < 8) uv = (cu(code - 4u) + cu(code - 3u)) * 0.5f;
            else {
                uv = glm::vec2(0.0f);
                for (uint32_t i = 0; i < n; ++i) uv += cu(i);
                uv /= float(n);
            }
            fine[f * 3 + size_t(c)] = uv;
        }
        outFlags[f] = 1;
    }
}

// --- Levels ---

// Shape and sculpt layers of level k from level k-1 and its details.
void evaluateLevel(MultiresStack& s, int k) {
    SubdivLevel& L = s.levels[size_t(k)];
    const Mesh& coarse = *s.levels[size_t(k) - 1].data;
    const SubdivTopology& tp = *L.topo;
    Mesh& fine = mutableData(L);
    const std::vector<glm::vec3> Q = subdivideField(tp, tp.smooth, baseChannel(coarse));
    std::vector<glm::vec3>& target = baseChannel(fine);
    if (L.detail) applyDetail(tp, fine.triangles, Q, *L.detail, target);
    else target = Q;
    for (size_t i = 0; i < fine.layers.size() && i < coarse.layers.size(); ++i) {
        std::vector<glm::vec3> d = subdivideField(tp, tp.smooth, coarse.layers[i].delta);
        if (i < L.layerDetail.size() && L.layerDetail[i])
            for (size_t v = 0; v < d.size(); ++v) d[v] += (*L.layerDetail[i])[v];
        fine.layers[i].delta = std::move(d);
    }
    evaluateLayers(fine);
    L.stale = false;
}

} // namespace

struct SubdivLevelCache {
    struct Entry {
        std::shared_ptr<Mesh> key; // the level data the mesh was made from (held, so it can't change in place)
        Mesh mesh;
        BVH bvh;
    };
    std::vector<Entry> entries; // most recently left last
    static constexpr size_t kMaxEntries = 2;
};

namespace {

// Makes the working mesh level `level` of its stack (levels up to it must be evaluated). `leaving` is the level
// data the working mesh was (null: don't keep it); with a BVH, that mesh and its BVH go to the level cache and
// a cached copy of the new level is used when there is one.
void enterLevel(Mesh& mesh, int level, BVH* bvh, std::shared_ptr<Mesh> leaving = nullptr) {
    std::shared_ptr<MultiresStack> stack = mesh.multires;
    if (leaving && leaving == stack->levels[size_t(level)].data) return; // already that level (e.g. Del Lower)
    if (!stack->cache) stack->cache = std::make_shared<SubdivLevelCache>();
    SubdivLevelCache& cache = *stack->cache;
    std::shared_ptr<PaintTexture> texture = mesh.texture;
    const bool toTexture = mesh.paintToTexture;
    const std::shared_ptr<Mesh> want = stack->levels[size_t(level)].data;
    // A cached copy of this level: exact (same data), or with the same triangles (only the shape or attributes
    // changed, so its topology and BVH are kept and refitted).
    std::optional<SubdivLevelCache::Entry> taken;
    bool exact = false;
    if (bvh) {
        auto hit = std::find_if(cache.entries.begin(), cache.entries.end(), [&](const auto& e) { return e.key == want; });
        exact = hit != cache.entries.end();
        if (!exact)
            hit = std::find_if(cache.entries.begin(), cache.entries.end(),
                               [&](const auto& e) { return sameArray(e.mesh.triangles, want->triangles); });
        if (hit != cache.entries.end()) {
            taken = std::move(*hit);
            cache.entries.erase(hit);
        }
        if (leaving) {
            // Keep the mesh being left (it has its topology and BVH already) for stepping back.
            SubdivLevelCache::Entry e{std::move(leaving), std::move(mesh), std::move(*bvh)};
            e.mesh.multires.reset();
            e.mesh.texture.reset();
            cache.entries.push_back(std::move(e));
            while (cache.entries.size() > SubdivLevelCache::kMaxEntries) cache.entries.erase(cache.entries.begin());
        }
    }
    if (taken) {
        mesh = std::move(taken->mesh);
        *bvh = std::move(taken->bvh);
        if (!exact) {
            std::vector<uint32_t> moved;
            for (uint32_t v = 0; v < want->positions.size(); ++v)
                if (want->positions[v] != mesh.positions[v]) moved.push_back(v);
            assignLevelArrays(mesh, *want);
            mesh.sanitizeAttributes();
            mesh.recomputeAllNormals();
            if (hasPaintLayers(mesh)) compositePaintAll(mesh);
            bvh->refit(mesh, moved);
        }
    } else {
        mesh = leanCopy(*want);
    }
    mesh.multires = std::move(stack);
    mesh.texture = std::move(texture);
    mesh.paintToTexture = toTexture;
    if (!taken) {
        mesh.rebuildTopology();
        mesh.recomputeAllNormals();
        if (hasPaintLayers(mesh)) compositePaintAll(mesh);
        if (bvh) bvh->build(mesh);
    }
    mesh.topologyDirty = true;
    mesh.facesDirty = true;
    mesh.markAllDirty();
}

bool sameLayerSet(const Mesh& a, const Mesh& b) {
    if (a.layers.size() != b.layers.size()) return false;
    for (size_t i = 0; i < a.layers.size(); ++i)
        if (a.layers[i].name != b.layers[i].name) return false;
    return true;
}
bool sameLayerWeights(const Mesh& a, const Mesh& b) {
    for (size_t i = 0; i < a.layers.size() && i < b.layers.size(); ++i)
        if (a.layers[i].weight != b.layers[i].weight) return false;
    return true;
}
bool samePaintSet(const Mesh& a, const Mesh& b) {
    if (a.paintLayers.size() != b.paintLayers.size() || a.paintBase.empty() != b.paintBase.empty()) return false;
    for (size_t i = 0; i < a.paintLayers.size(); ++i)
        if (a.paintLayers[i].name != b.paintLayers[i].name) return false;
    return true;
}
bool samePaintProperties(const Mesh& a, const Mesh& b) {
    if (a.activePaintLayer != b.activePaintLayer) return false;
    for (size_t i = 0; i < a.paintLayers.size() && i < b.paintLayers.size(); ++i) {
        const PaintLayer &x = a.paintLayers[i], &y = b.paintLayers[i];
        if (x.opacity != y.opacity || x.blend != y.blend || x.visible != y.visible || x.locked != y.locked) return false;
    }
    return true;
}

// Gives `m` (a level with `nv` vertices) the sculpt layer list of `w`, matching layers by name; new ones are zero.
void remapLayers(Mesh& m, std::vector<std::shared_ptr<std::vector<glm::vec3>>>* detail, const Mesh& w) {
    const size_t nv = m.positions.size();
    std::vector<SculptLayer> layers;
    std::vector<std::shared_ptr<std::vector<glm::vec3>>> details;
    std::vector<bool> used(m.layers.size(), false);
    for (const SculptLayer& wl : w.layers) {
        SculptLayer l{wl.name, {}, wl.weight};
        std::shared_ptr<std::vector<glm::vec3>> d;
        for (size_t i = 0; i < m.layers.size(); ++i)
            if (!used[i] && m.layers[i].name == wl.name) {
                used[i] = true;
                l.delta = std::move(m.layers[i].delta);
                if (detail && i < detail->size()) d = (*detail)[i];
                break;
            }
        if (l.delta.size() != nv) l.delta.assign(nv, glm::vec3(0.0f));
        layers.push_back(std::move(l));
        details.push_back(std::move(d));
    }
    if (m.layers.empty() && !layers.empty()) m.base = m.positions;
    m.layers = std::move(layers);
    if (m.layers.empty()) {
        if (m.base.size() == nv) m.positions = m.base; // the layers' offsets are dropped, as removeLayer does
        m.base.clear();
    }
    if (detail) *detail = std::move(details);
    evaluateLayers(m);
}

// Gives `m` the paint layer stack of `w` (names, order, settings), matching layers by name; new ones are transparent.
void remapPaint(Mesh& m, const Mesh& w) {
    const size_t nv = m.positions.size();
    if (w.paintLayers.empty()) {
        std::vector<PaintLayer>().swap(m.paintLayers);
        std::vector<glm::vec3>().swap(m.paintBase);
        m.activePaintLayer = -1;
        return;
    }
    if (m.paintBase.size() != nv) m.paintBase = m.colors.size() == nv ? m.colors : std::vector<glm::vec3>(nv, glm::vec3(1.0f));
    std::vector<PaintLayer> layers;
    std::vector<bool> used(m.paintLayers.size(), false);
    for (const PaintLayer& wl : w.paintLayers) {
        PaintLayer l = wl;
        l.color.clear();
        l.alpha.clear();
        for (size_t i = 0; i < m.paintLayers.size(); ++i)
            if (!used[i] && m.paintLayers[i].name == wl.name) {
                used[i] = true;
                l.color = std::move(m.paintLayers[i].color);
                l.alpha = std::move(m.paintLayers[i].alpha);
                break;
            }
        if (l.color.size() != nv) l.color.assign(nv, glm::vec3(1.0f));
        if (l.alpha.size() != nv) l.alpha.assign(nv, 0.0f);
        layers.push_back(std::move(l));
    }
    m.paintLayers = std::move(layers);
    m.activePaintLayer = w.activePaintLayer;
}

// Spreads a change of one per-vertex array from the current level `c` to every other level: copied down to
// the parent vertices, interpolated up where it changed. `get` picks the array from a level.
template <class T, class Get>
void spreadVertexArray(MultiresStack& s, int c, const std::vector<T>& w, const std::vector<T>& r, T fill, Get get) {
    const int n = int(s.levels.size());
    if (w.empty() && r.empty()) return;
    if (w.empty()) {
        for (int j = 0; j < n; ++j)
            if (j != c) std::vector<T>().swap(get(mutableData(s.levels[size_t(j)])));
        return;
    }
    std::vector<uint8_t> changed(w.size(), 0);
    bool any = false;
    for (size_t v = 0; v < w.size(); ++v)
        if (r.size() != w.size() ? !(w[v] == fill) : !(w[v] == r[v])) changed[v] = 1, any = true;
    if (!any && r.size() == w.size()) return;
    std::vector<uint8_t> flags = changed, next;
    const std::vector<T>* fine = &w;
    for (int j = c; j >= 1; --j) {
        std::vector<T>& coarse = get(mutableData(s.levels[size_t(j) - 1]));
        const bool fresh = coarse.empty();
        restrictVertices(*s.levels[size_t(j)].topo, *fine, coarse, fill, fresh ? nullptr : &flags, next);
        flags.swap(next);
        fine = &coarse;
    }
    flags = changed;
    const std::vector<T>* coarse = &w;
    for (int j = c + 1; j < n; ++j) {
        std::vector<T>& f = get(mutableData(s.levels[size_t(j)]));
        prolongVertices(*s.levels[size_t(j)].topo, *coarse, f, fill, &flags, next);
        flags.swap(next);
        coarse = &f;
    }
}

template <class T, class Get>
void spreadFaceArray(MultiresStack& s, int c, const std::vector<T>& w, const std::vector<T>& r, T fill, Get get) {
    const int n = int(s.levels.size());
    if (w.empty() && r.empty()) return;
    if (w.empty()) {
        for (int j = 0; j < n; ++j)
            if (j != c) std::vector<T>().swap(get(mutableData(s.levels[size_t(j)])));
        return;
    }
    std::vector<uint8_t> changed(w.size(), 0);
    bool any = false;
    for (size_t t = 0; t < w.size(); ++t)
        if (r.size() != w.size() ? !(w[t] == fill) : !(w[t] == r[t])) changed[t] = 1, any = true;
    if (!any && r.size() == w.size()) return;
    std::vector<uint8_t> flags = changed, next;
    const std::vector<T>* fine = &w;
    for (int j = c; j >= 1; --j) {
        Mesh& cm = mutableData(s.levels[size_t(j) - 1]);
        std::vector<T>& coarse = get(cm);
        const bool fresh = coarse.empty();
        restrictFaces(*s.levels[size_t(j)].topo, *fine, coarse, cm.triangleCount(), fill, fresh ? nullptr : &flags, next);
        flags.swap(next);
        fine = &coarse;
    }
    flags = changed;
    const std::vector<T>* coarse = &w;
    for (int j = c + 1; j < n; ++j) {
        std::vector<T>& f = get(mutableData(s.levels[size_t(j)]));
        prolongFaces(*s.levels[size_t(j)].topo, *coarse, f, fill, &flags, next);
        flags.swap(next);
        coarse = &f;
    }
}

void spreadUvs(MultiresStack& s, int c, const Mesh& w, const Mesh& r) {
    const int n = int(s.levels.size());
    if (w.uvs.empty() && r.uvs.empty()) return;
    if (w.uvs.empty()) {
        for (int j = 0; j < n; ++j)
            if (j != c) std::vector<glm::vec2>().swap(mutableData(s.levels[size_t(j)]).uvs);
        return;
    }
    const size_t nt = w.triangleCount();
    std::vector<uint8_t> changed(nt, 0);
    bool any = false;
    for (size_t t = 0; t < nt; ++t)
        if (r.uvs.size() != w.uvs.size() || w.uvs[t * 3] != r.uvs[t * 3] || w.uvs[t * 3 + 1] != r.uvs[t * 3 + 1] ||
            w.uvs[t * 3 + 2] != r.uvs[t * 3 + 2])
            changed[t] = 1, any = true;
    if (!any) return;
    std::vector<uint8_t> flags = changed, next;
    const std::vector<glm::vec2>* fine = &w.uvs;
    for (int j = c; j >= 1; --j) {
        Mesh& cm = mutableData(s.levels[size_t(j) - 1]);
        const bool fresh = cm.uvs.empty();
        restrictUvs(*s.levels[size_t(j)].topo, cm, *fine, cm.uvs, fresh ? nullptr : &flags, next);
        flags.swap(next);
        fine = &cm.uvs;
    }
    flags = changed;
    const std::vector<glm::vec2>* coarse = &w.uvs;
    for (int j = c + 1; j < n; ++j) {
        std::vector<glm::vec2>& f = mutableData(s.levels[size_t(j)]).uvs;
        prolongUvs(*s.levels[size_t(j)].topo, *coarse, f, &flags, next);
        flags.swap(next);
        coarse = &f;
    }
}

// Shape: restrict the change of each channel down, re-fit the details of the levels below, and mark the
// levels above for re-evaluation.
void spreadShape(MultiresStack& s, int c, const Mesh& w, const Mesh& r) {
    const size_t channels = 1 + w.layers.size();
    bool anyChange = false;
    for (size_t ch = 0; ch < channels; ++ch) {
        const std::vector<glm::vec3>& wa = ch == 0 ? baseChannel(w) : w.layers[ch - 1].delta;
        const std::vector<glm::vec3>& ra = ch == 0 ? baseChannel(r) : r.layers[ch - 1].delta;
        if (sameArray(wa, ra)) continue;
        anyChange = true;
        // Restrict: each coarse vertex moves by half its own change plus half the mean change of the edge
        // points around it, so a sharp stroke shows up softened instead of aliased.
        std::vector<glm::vec3> delta(wa.size());
        parallelRange(wa.size(), [&](size_t b, size_t e) {
            for (size_t v = b; v < e; ++v) delta[v] = wa[v] - ra[v];
        });
        for (int j = c; j >= 1; --j) {
            const SubdivTopology& tp = *s.levels[size_t(j)].topo;
            std::vector<glm::vec3> cd(tp.coarseVertices);
            parallelRange(tp.coarseVertices, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    const uint32_t e0 = tp.vertEdgeOffsets[v], e1 = tp.vertEdgeOffsets[v + 1];
                    const glm::vec3 own = delta[tp.vertexChild[v]];
                    if (e0 == e1) {
                        cd[v] = own;
                        continue;
                    }
                    glm::vec3 sum(0.0f);
                    for (uint32_t k = e0; k < e1; ++k) sum += delta[tp.edgeChild[tp.vertEdges[k]]];
                    cd[v] = 0.5f * own + 0.5f * sum / float(e1 - e0);
                }
            });
            Mesh& cm = mutableData(s.levels[size_t(j) - 1]);
            std::vector<glm::vec3>& target = ch == 0 ? baseChannel(cm) : cm.layers[ch - 1].delta;
            for (size_t v = 0; v < cd.size(); ++v) target[v] += cd[v];
            delta = std::move(cd);
        }
        // Re-fit the details bottom-up so each level is exactly what it was (the current one exactly as sculpted).
        for (int j = 1; j <= c; ++j) {
            SubdivLevel& L = s.levels[size_t(j)];
            const SubdivTopology& tp = *L.topo;
            const Mesh& cm = *s.levels[size_t(j) - 1].data;
            const Mesh& fm = j == c ? w : *L.data;
            const std::vector<glm::vec3>& fineVals = ch == 0 ? baseChannel(fm) : fm.layers[ch - 1].delta;
            const std::vector<glm::vec3> Q = subdivideField(tp, tp.smooth, ch == 0 ? baseChannel(cm) : cm.layers[ch - 1].delta);
            if (ch == 0) {
                fitDetail(tp, fm.triangles, Q, fineVals, mutableArray(L.detail, Q.size()));
            } else {
                if (L.layerDetail.size() < w.layers.size()) L.layerDetail.resize(w.layers.size());
                std::vector<glm::vec3>& d = mutableArray(L.layerDetail[ch - 1], Q.size());
                for (size_t v = 0; v < Q.size(); ++v) d[v] = fineVals[v] - Q[v];
            }
        }
    }
    if (!anyChange) return;
    for (int j = 0; j < c; ++j) evaluateLayers(mutableData(s.levels[size_t(j)]));
    for (size_t j = size_t(c) + 1; j < s.levels.size(); ++j) s.levels[j].stale = true;
}

bool levelsDiffer(const Mesh& w, const Mesh& r) {
    if (!sameArray(w.positions, r.positions) || !sameArray(w.base, r.base) || !sameLayerSet(w, r) ||
        !sameLayerWeights(w, r) || !sameArray(w.mask, r.mask) || !sameArray(w.colors, r.colors) ||
        !sameArray(w.polygroups, r.polygroups) || !sameArray(w.hidden, r.hidden) || !sameArray(w.uvs, r.uvs) ||
        !samePaintSet(w, r) || !samePaintProperties(w, r) || !sameArray(w.paintBase, r.paintBase))
        return true;
    for (size_t i = 0; i < w.layers.size(); ++i)
        if (!sameArray(w.layers[i].delta, r.layers[i].delta)) return true;
    for (size_t i = 0; i < w.paintLayers.size(); ++i)
        if (!sameArray(w.paintLayers[i].color, r.paintLayers[i].color) ||
            !sameArray(w.paintLayers[i].alpha, r.paintLayers[i].alpha))
            return true;
    return false;
}

// The levels a mesh with no stack starts with: just itself.
MultiresStack& ensureStack(Mesh& mesh) {
    if (!mesh.multires) {
        mesh.multires = std::make_shared<MultiresStack>();
        mesh.multires->levels.push_back({leanShared(mesh), nullptr, nullptr, {}, false});
        mesh.multires->current = 0;
    }
    return mutableStack(mesh);
}

void dropStackIfSingle(Mesh& mesh) {
    if (mesh.multires && mesh.multires->levels.size() < 2) mesh.multires.reset();
}

// The fine level of a divide: triangles, quads and every attribute from the coarse level.
std::shared_ptr<Mesh> divideData(SubdivTopology& tp, const Mesh& coarse) {
    auto fine = std::make_shared<Mesh>();
    buildDividedFaces(tp, coarse, *fine);
    std::vector<uint8_t> f;
    if (coarse.layers.empty()) {
        fine->positions = subdivideField(tp, tp.smooth, coarse.positions);
    } else {
        fine->base = subdivideField(tp, tp.smooth, coarse.base);
        for (const SculptLayer& l : coarse.layers) fine->layers.push_back({l.name, subdivideField(tp, tp.smooth, l.delta), l.weight});
        fine->positions.resize(fine->base.size());
        evaluateLayers(*fine);
    }
    if (coarse.hasMask()) prolongVertices(tp, coarse.mask, fine->mask, 0.0f, nullptr, f);
    if (coarse.hasColors()) prolongVertices(tp, coarse.colors, fine->colors, glm::vec3(1.0f), nullptr, f);
    if (coarse.hasPolygroups()) prolongFaces(tp, coarse.polygroups, fine->polygroups, 0u, nullptr, f);
    if (coarse.hasHidden()) prolongFaces(tp, coarse.hidden, fine->hidden, uint8_t(0), nullptr, f);
    if (coarse.hasUvs()) prolongUvs(tp, coarse.uvs, fine->uvs, nullptr, f);
    if (!coarse.paintLayers.empty()) {
        prolongVertices(tp, coarse.paintBase, fine->paintBase, glm::vec3(1.0f), nullptr, f);
        for (const PaintLayer& l : coarse.paintLayers) {
            PaintLayer nl = l;
            nl.color.clear();
            nl.alpha.clear();
            prolongPaintLayer(tp, l, nl, nullptr);
            fine->paintLayers.push_back(std::move(nl));
        }
        fine->activePaintLayer = coarse.activePaintLayer;
    }
    return fine;
}

} // namespace

size_t SubdivTopology::bytes() const {
    auto b = [](const auto& v) { return v.size() * sizeof(v[0]); };
    return b(pairing) + b(polyOffsets) + b(polyVerts) + b(polyEdges) + b(polyCorner) + b(polyTris) + b(edges) +
           b(edgeFaces) + b(vertEdgeOffsets) + b(vertEdges) + b(vertexChild) + b(edgeChild) + b(faceChild) +
           b(fineParentPoly) + b(fineParentTri) + b(fineCode) + b(frameRef);
}

// --- Queries ---

int subdivLevelCount(const Mesh& mesh) { return mesh.multires ? int(mesh.multires->levels.size()) : 0; }
int subdivCurrentLevel(const Mesh& mesh) { return mesh.multires ? mesh.multires->current : 0; }

std::string subdivLevelText(const Mesh& mesh) {
    if (!mesh.multires) return {};
    return "SDiv " + std::to_string(mesh.multires->current + 1) + "/" + std::to_string(mesh.multires->levels.size());
}

size_t subdivLevelTriangles(const Mesh& mesh, int level) {
    if (!mesh.multires) return level == 0 ? mesh.triangleCount() : 0;
    if (level < 0 || level >= int(mesh.multires->levels.size())) return 0;
    return mesh.multires->levels[size_t(level)].data->triangleCount();
}

bool subdivMatches(const Mesh& mesh) {
    if (!mesh.multires) return true;
    const MultiresStack& s = *mesh.multires;
    if (s.current < 0 || s.current >= int(s.levels.size()) || !s.levels[size_t(s.current)].data) return false;
    const Mesh& r = *s.levels[size_t(s.current)].data;
    return r.vertexCount() == mesh.vertexCount() && r.triangleCount() == mesh.triangleCount();
}

size_t subdivBytes(const Mesh& mesh) {
    if (!mesh.multires) return 0;
    size_t total = 0;
    for (const SubdivLevel& l : mesh.multires->levels) {
        const Mesh& m = *l.data;
        total += m.positions.size() * sizeof(glm::vec3) * (1 + (m.base.empty() ? 0 : 1 + m.layers.size())) +
                 m.triangles.size() * sizeof(glm::uvec3) + m.mask.size() * 4 + m.colors.size() * 12 +
                 m.polygroups.size() * 4 + m.hidden.size() + m.uvs.size() * 8 + m.quadPartners.size() * 4 +
                 m.paintBase.size() * 12;
        for (const PaintLayer& p : m.paintLayers) total += p.color.size() * 12 + p.alpha.size() * 4;
        if (l.topo) total += l.topo->bytes();
        if (l.detail) total += l.detail->size() * sizeof(glm::vec3);
        for (const auto& d : l.layerDetail)
            if (d) total += d->size() * sizeof(glm::vec3);
    }
    return total;
}

// --- Operations ---

bool subdivCommit(Mesh& w) {
    if (!w.multires) return true;
    const int c = w.multires->current;
    const std::shared_ptr<Mesh> rp = w.multires->levels[size_t(c)].data;
    const Mesh& r0 = *rp;
    if (w.vertexCount() != r0.vertexCount() || !sameArray(w.triangles, r0.triangles)) {
        w.multires.reset(); // the topology changed under the stack: the levels no longer apply
        return false;
    }
    if (!levelsDiffer(w, r0)) return true;
    MultiresStack& s = mutableStack(w);
    const int n = int(s.levels.size());

    // Sculpt and paint layer sets first, so every level has the current level's lists (r = the old current
    // level with the new lists, to diff against).
    Mesh remapped;
    const Mesh* r = &r0;
    if (!sameLayerSet(w, r0) || !samePaintSet(w, r0)) {
        remapped = leanCopy(r0);
        if (!sameLayerSet(w, r0)) {
            // `remapped` also carries the current level's layer details through the same name mapping.
            remapLayers(remapped, c > 0 ? &s.levels[size_t(c)].layerDetail : nullptr, w);
            for (int j = 0; j < n; ++j)
                if (j != c) remapLayers(mutableData(s.levels[size_t(j)]), &s.levels[size_t(j)].layerDetail, w);
        }
        if (!samePaintSet(w, r0)) {
            remapPaint(remapped, w);
            for (int j = 0; j < n; ++j)
                if (j != c) remapPaint(mutableData(s.levels[size_t(j)]), w);
        }
        r = &remapped;
    }
    if (!sameLayerWeights(w, *r)) {
        for (int j = 0; j < n; ++j) {
            if (j == c) continue;
            Mesh& m = mutableData(s.levels[size_t(j)]);
            for (size_t i = 0; i < m.layers.size() && i < w.layers.size(); ++i) m.layers[i].weight = w.layers[i].weight;
            if (!s.levels[size_t(j)].stale) evaluateLayers(m);
        }
    }
    if (!samePaintProperties(w, *r)) {
        for (int j = 0; j < n; ++j) {
            if (j == c) continue;
            Mesh& m = mutableData(s.levels[size_t(j)]);
            m.activePaintLayer = w.activePaintLayer;
            for (size_t i = 0; i < m.paintLayers.size() && i < w.paintLayers.size(); ++i) {
                m.paintLayers[i].opacity = w.paintLayers[i].opacity;
                m.paintLayers[i].blend = w.paintLayers[i].blend;
                m.paintLayers[i].visible = w.paintLayers[i].visible;
                m.paintLayers[i].locked = w.paintLayers[i].locked;
            }
        }
    }

    spreadShape(s, c, w, *r);
    spreadVertexArray(s, c, w.mask, r->mask, 0.0f, [](Mesh& m) -> std::vector<float>& { return m.mask; });
    if (w.paintLayers.empty()) {
        spreadVertexArray(s, c, w.colors, r->colors, glm::vec3(1.0f), [](Mesh& m) -> std::vector<glm::vec3>& { return m.colors; });
    } else {
        spreadVertexArray(s, c, w.paintBase, r->paintBase, glm::vec3(1.0f),
                          [](Mesh& m) -> std::vector<glm::vec3>& { return m.paintBase; });
        for (size_t i = 0; i < w.paintLayers.size(); ++i) {
            spreadVertexArray(s, c, w.paintLayers[i].color, r->paintLayers[i].color, glm::vec3(1.0f),
                              [i](Mesh& m) -> std::vector<glm::vec3>& { return m.paintLayers[i].color; });
            spreadVertexArray(s, c, w.paintLayers[i].alpha, r->paintLayers[i].alpha, 0.0f,
                              [i](Mesh& m) -> std::vector<float>& { return m.paintLayers[i].alpha; });
        }
        for (int j = 0; j < n; ++j)
            if (j != c && hasPaintLayers(*s.levels[size_t(j)].data)) compositePaintAll(mutableData(s.levels[size_t(j)]));
    }
    spreadFaceArray(s, c, w.polygroups, r->polygroups, 0u, [](Mesh& m) -> std::vector<uint32_t>& { return m.polygroups; });
    spreadFaceArray(s, c, w.hidden, r->hidden, uint8_t(0), [](Mesh& m) -> std::vector<uint8_t>& { return m.hidden; });
    spreadUvs(s, c, w, *r);

    s.levels[size_t(c)].data = leanShared(w);
    return true;
}

bool subdivSetLevel(Mesh& mesh, int level, std::string& error, BVH* bvh) {
    if (!mesh.multires) {
        error = "This SubTool has no subdivision levels (Divide adds them)";
        return false;
    }
    if (!subdivCommit(mesh)) {
        error = "The mesh topology changed, so its subdivision levels were dropped";
        return false;
    }
    const int n = int(mesh.multires->levels.size());
    if (level < 0 || level >= n) {
        error = "No subdivision level " + std::to_string(level + 1) + " (this SubTool has " + std::to_string(n) + ")";
        return false;
    }
    if (level == mesh.multires->current) return true;
    MultiresStack& s = mutableStack(mesh);
    for (int k = s.current + 1; k <= level; ++k)
        if (s.levels[size_t(k)].stale) evaluateLevel(s, k);
    std::shared_ptr<Mesh> leaving = s.levels[size_t(s.current)].data;
    s.current = level;
    enterLevel(mesh, level, bvh, std::move(leaving));
    return true;
}

bool subdivDivide(Mesh& mesh, bool smooth, std::string& error, BVH* bvh, size_t maxTriangles) {
    if (mesh.triangleCount() == 0) {
        error = "Nothing to divide";
        return false;
    }
    if (mesh.multires) {
        if (!subdivCommit(mesh)) {
            // fall through: the stale stack was dropped, start a new one from this mesh
        } else if (mesh.multires->current != int(mesh.multires->levels.size()) - 1 &&
                   !subdivSetLevel(mesh, int(mesh.multires->levels.size()) - 1, error, bvh)) {
            return false;
        }
    }
    const bool cc = mesh.quadCount() > 0 || (mesh.multires && mesh.multires->levels.size() > 1 &&
                                             mesh.multires->levels.back().topo->catmullClark);
    // Projected size: Catmull-Clark makes 2n triangles per n-gon, Loop 4 per triangle.
    size_t projected = 0;
    if (cc) projected = mesh.quadCount() * 8 + (mesh.triangleCount() - 2 * mesh.quadCount()) * 6;
    else projected = mesh.triangleCount() * 4;
    if (projected > maxTriangles) {
        error = "Dividing would make " + std::to_string(projected) + " triangles (the limit is " + std::to_string(maxTriangles) + ")";
        return false;
    }
    MultiresStack& s = ensureStack(mesh);
    std::shared_ptr<Mesh> leaving = s.levels.back().data;
    const Mesh& coarse = *leaving;
    std::vector<uint32_t> pairing;
    if (cc) {
        pairing.resize(coarse.triangleCount());
        for (uint32_t t = 0; t < pairing.size(); ++t) pairing[t] = coarse.quadPartnerAt(t);
    }
    std::shared_ptr<SubdivTopology> tp = buildCoarse(coarse, std::move(pairing), cc, smooth);
    SubdivLevel level;
    level.data = divideData(*tp, coarse);
    level.topo = tp;
    level.layerDetail.resize(coarse.layers.size());
    s.levels.push_back(std::move(level));
    s.current = int(s.levels.size()) - 1;
    enterLevel(mesh, s.current, bvh, std::move(leaving));
    return true;
}

bool subdivDeleteLower(Mesh& mesh, std::string& error) {
    if (!mesh.multires || !subdivCommit(mesh)) {
        error = "This SubTool has no subdivision levels";
        return false;
    }
    if (mesh.multires->current == 0) {
        error = "There are no lower levels (this is SDiv 1)";
        return false;
    }
    MultiresStack& s = mutableStack(mesh);
    s.levels.erase(s.levels.begin(), s.levels.begin() + s.current);
    s.current = 0;
    s.levels[0].topo.reset();
    s.levels[0].detail.reset();
    s.levels[0].layerDetail.clear();
    dropStackIfSingle(mesh);
    return true;
}

bool subdivDeleteHigher(Mesh& mesh, std::string& error) {
    if (!mesh.multires || !subdivCommit(mesh)) {
        error = "This SubTool has no subdivision levels";
        return false;
    }
    if (mesh.multires->current == int(mesh.multires->levels.size()) - 1) {
        error = "There are no higher levels (this is the highest)";
        return false;
    }
    MultiresStack& s = mutableStack(mesh);
    s.levels.resize(size_t(s.current) + 1);
    dropStackIfSingle(mesh);
    return true;
}

Mesh subdivLevelMesh(const Mesh& mesh, int level) {
    Mesh m = leanCopy(mesh);
    m.multires = mesh.multires;
    m.texture = mesh.texture;
    m.paintToTexture = mesh.paintToTexture;
    std::string error;
    if (!m.multires || level == subdivCurrentLevel(mesh) || !subdivSetLevel(m, level, error)) {
        m = leanCopy(mesh);
        m.texture = mesh.texture;
        m.paintToTexture = mesh.paintToTexture;
        m.rebuildTopology();
        m.recomputeAllNormals();
    }
    m.multires.reset();
    return m;
}

// --- Project files ---

std::shared_ptr<const SubdivTopology> rebuildSubdivTopology(const Mesh& coarse, const Mesh& fine,
                                                            std::vector<uint32_t> pairing, bool catmullClark,
                                                            bool smooth, std::vector<uint32_t> vertexChild,
                                                            std::vector<uint32_t> edgeChild,
                                                            std::vector<uint32_t> faceChild, std::string& error) {
    std::shared_ptr<SubdivTopology> tp = buildCoarse(coarse, std::move(pairing), catmullClark, smooth);
    tp->vertexChild = std::move(vertexChild);
    tp->edgeChild = std::move(edgeChild);
    tp->faceChild = std::move(faceChild);
    if (tp->fineVertices != fine.vertexCount()) {
        error = "subdivision level vertex counts don't match";
        return nullptr;
    }
    if (!buildFineMetadata(*tp, coarse, fine.triangles, error)) return nullptr;
    return tp;
}

bool attachSubdivStack(Mesh& mesh, MultiresStack stack, std::string& error) {
    const size_t n = stack.levels.size();
    if (n < 2 || stack.current < 0 || size_t(stack.current) >= n) {
        error = "invalid subdivision levels";
        return false;
    }
    for (size_t j = 0; j < n; ++j) {
        const SubdivLevel& l = stack.levels[j];
        if (!l.data || (j > 0 && (!l.topo || l.topo->fineVertices != l.data->vertexCount() ||
                                  l.topo->fineParentTri.size() != l.data->triangleCount())) ||
            (l.detail && l.detail->size() != l.data->vertexCount())) {
            error = "subdivision level " + std::to_string(j + 1) + " doesn't fit the level below";
            return false;
        }
        for (const auto& d : l.layerDetail)
            if (d && d->size() != l.data->vertexCount()) {
                error = "subdivision level " + std::to_string(j + 1) + " has broken layer details";
                return false;
            }
        if (j <= size_t(stack.current) && l.stale) {
            error = "subdivision levels are out of date";
            return false;
        }
    }
    const Mesh& cur = *stack.levels[size_t(stack.current)].data;
    if (cur.vertexCount() != mesh.vertexCount() || cur.triangles != mesh.triangles) {
        error = "the current subdivision level doesn't match the mesh";
        return false;
    }
    mesh.multires = std::make_shared<MultiresStack>(std::move(stack));
    return true;
}

const Mesh& subdivLevelData(const Mesh& mesh, int level) { return *mesh.multires->levels[size_t(level)].data; }

bool subdivInSync(const Mesh& mesh) {
    if (!mesh.multires) return true;
    const Mesh& r = *mesh.multires->levels[size_t(mesh.multires->current)].data;
    return sameArray(mesh.triangles, r.triangles) && !levelsDiffer(mesh, r);
}

// --- Reconstruct Subdiv ---

namespace {

struct Reconstruction {
    Mesh coarse;                                 // positions = the fine corner positions, attributes from the fine level
    std::vector<uint32_t> pairing;               // coarse quad pairs
    std::vector<uint32_t> vertexChild, faceChild; // fine vertex of each coarse vertex / polygon
    std::unordered_map<uint64_t, uint32_t> edgePoint; // coarse vertex pair -> fine edge vertex
    bool catmullClark = false;
};

uint64_t pairKey(uint32_t a, uint32_t b) { return a < b ? (uint64_t(a) << 32 | b) : (uint64_t(b) << 32 | a); }

enum Label : uint8_t { kUnset = 0, kO = 1, kE = 2, kF = 3 };

// All-quad mesh: label vertices O (original), E (edge) and F (face) so that every quad reads O, E, F, E.
bool reconstructCatmullClark(const Mesh& m, Reconstruction& out, std::string& error) {
    std::vector<uint32_t> pairing(m.triangleCount());
    for (uint32_t t = 0; t < pairing.size(); ++t) {
        pairing[t] = m.quadPartnerAt(t);
        if (pairing[t] == Mesh::kNoQuad) {
            error = "Reconstruct needs an all-quad mesh (or an all-triangle one)";
            return false;
        }
    }
    const std::shared_ptr<SubdivTopology> w = buildCoarse(m, pairing, true, true);
    const size_t np = w->polygonCount();
    for (size_t p = 0; p < np; ++p)
        if (polySize(*w, p) != 4) {
            error = "Reconstruct needs an all-quad mesh";
            return false;
        }
    const uint32_t nv = uint32_t(m.vertexCount());
    std::vector<uint8_t> label(nv, kUnset), polyDone(np, 0);
    std::vector<uint32_t> touched, queue;
    auto label4 = [&](uint32_t p, std::vector<uint8_t>& lab) -> int { // labels poly p from one known O/F corner
        const uint32_t o = w->polyOffsets[p];
        for (uint32_t k = 0; k < 4; ++k) {
            const uint8_t L = lab[w->polyVerts[o + k]];
            if (L != kO && L != kF) continue;
            const uint8_t want[4] = {L, kE, uint8_t(L == kO ? kF : kO), kE};
            for (uint32_t i = 0; i < 4; ++i) {
                const uint32_t v = w->polyVerts[o + (k + i) % 4];
                if (lab[v] == kUnset) {
                    lab[v] = want[i];
                    touched.push_back(v);
                } else if (lab[v] != want[i]) {
                    return -1;
                }
            }
            return 1;
        }
        return 0;
    };
    for (uint32_t seed = 0; seed < np; ++seed) {
        if (polyDone[seed]) continue;
        // Four ways to label the seed quad; keep the consistent one whose face points look most like quads.
        int best = -1;
        long bestScore = -1;
        std::vector<uint8_t> bestLabels;
        std::vector<uint32_t> bestPolys;
        for (int option = 0; option < 4; ++option) {
            std::vector<uint8_t> lab = label;
            touched.clear();
            const uint32_t o = w->polyOffsets[seed];
            lab[w->polyVerts[o + uint32_t(option / 2)]] = option % 2 ? kF : kO;
            if (label[w->polyVerts[o + uint32_t(option / 2)]] != kUnset) continue;
            touched.push_back(w->polyVerts[o + uint32_t(option / 2)]);
            std::vector<uint8_t> seen(np, 0);
            std::vector<uint32_t> polys;
            queue.assign(1, seed);
            seen[seed] = 1;
            bool ok = true;
            while (!queue.empty() && ok) {
                const uint32_t p = queue.back();
                queue.pop_back();
                if (label4(p, lab) != 1) {
                    ok = false;
                    break;
                }
                polys.push_back(p);
                for (uint32_t i = w->polyOffsets[p]; i < w->polyOffsets[p + 1]; ++i) {
                    const glm::uvec2 f = w->edgeFaces[w->polyEdges[i]];
                    for (uint32_t q : {f.x, f.y})
                        if (q != kNone && !seen[q]) {
                            seen[q] = 1;
                            queue.push_back(q);
                        }
                }
            }
            if (!ok) continue;
            long score = 0; // face points of valence 4 (coarse quads) first
            for (uint32_t v : touched)
                if (lab[v] == kF) score += (w->vertEdgeOffsets[v + 1] - w->vertEdgeOffsets[v]) == 4 ? 2 : 1;
            if (score > bestScore) {
                best = option;
                bestScore = score;
                bestLabels = std::move(lab);
                bestPolys = std::move(polys);
            }
        }
        if (best < 0) {
            error = "The mesh is not a regular Catmull-Clark subdivision (no consistent corner / edge / face pattern)";
            return false;
        }
        label = std::move(bestLabels);
        for (uint32_t p : bestPolys) polyDone[p] = 1;
    }
    // Coarse vertices: the O vertices in index order.
    std::vector<uint32_t> coarseIndex(nv, kNone);
    for (uint32_t v = 0; v < nv; ++v) {
        if (label[v] == kUnset) {
            error = "The mesh has loose vertices";
            return false;
        }
        if (label[v] == kO) {
            coarseIndex[v] = uint32_t(out.vertexChild.size());
            out.vertexChild.push_back(v);
        }
    }
    // Quads around each F vertex: (O, e_i, F, e_{i-1}); walk them in order to get the coarse polygon.
    std::vector<uint32_t> fquads; // per poly: index of its F corner
    std::vector<std::vector<uint32_t>> around(nv);
    for (uint32_t p = 0; p < np; ++p) {
        const uint32_t o = w->polyOffsets[p];
        for (uint32_t k = 0; k < 4; ++k)
            if (label[w->polyVerts[o + k]] == kF) around[w->polyVerts[o + k]].push_back(p * 4 + k);
    }
    out.catmullClark = true;
    for (uint32_t f = 0; f < nv; ++f) {
        if (label[f] != kF) continue;
        const auto& qs = around[f];
        if (qs.size() < 3 || qs.size() > 4) {
            error = "The mesh is not a regular subdivision (a face point joins " + std::to_string(qs.size()) + " quads)";
            return false;
        }
        auto at = [&](uint32_t q, uint32_t d) { return w->polyVerts[w->polyOffsets[q / 4] + (q % 4 + d) % 4]; };
        std::vector<uint32_t> corners, edgesPts;
        uint32_t q = qs[0];
        for (size_t step = 0; step < qs.size(); ++step) {
            corners.push_back(at(q, 2));      // O
            edgesPts.push_back(at(q, 3));     // e_i, towards the next corner
            const uint32_t next = at(q, 3);
            uint32_t found = kNone;
            for (uint32_t r : qs)
                if (at(r, 1) == next) found = r;
            if (found == kNone) {
                error = "The mesh is not a regular subdivision (open face fan)";
                return false;
            }
            q = found;
        }
        if (q != qs[0]) {
            error = "The mesh is not a regular subdivision (face fan does not close)";
            return false;
        }
        const size_t n = corners.size();
        const uint32_t firstTri = uint32_t(out.coarse.triangles.size());
        for (size_t i = 0; i < n; ++i) {
            const uint32_t a = coarseIndex[corners[i]], b = coarseIndex[corners[(i + 1) % n]];
            auto [it, inserted] = out.edgePoint.try_emplace(pairKey(a, b), edgesPts[i]);
            if (!inserted && it->second != edgesPts[i]) {
                error = "The mesh is not a regular subdivision (edge points disagree)";
                return false;
            }
        }
        if (n == 4) {
            out.coarse.triangles.push_back({coarseIndex[corners[0]], coarseIndex[corners[1]], coarseIndex[corners[2]]});
            out.coarse.triangles.push_back({coarseIndex[corners[0]], coarseIndex[corners[2]], coarseIndex[corners[3]]});
            out.pairing.push_back(firstTri + 1);
            out.pairing.push_back(firstTri);
        } else {
            out.coarse.triangles.push_back({coarseIndex[corners[0]], coarseIndex[corners[1]], coarseIndex[corners[2]]});
            out.pairing.push_back(kNone);
        }
        out.faceChild.push_back(f);
    }
    return true;
}

// All-triangle mesh: label vertices O / E so every triangle is a corner (O, E, E) or a centre (E, E, E) one.
bool reconstructLoop(const Mesh& m, Reconstruction& out, std::string& error) {
    const std::shared_ptr<SubdivTopology> w = buildCoarse(m, {}, false, true);
    const uint32_t nv = uint32_t(m.vertexCount()), nt = uint32_t(m.triangleCount());
    std::vector<uint8_t> label(nv, kUnset), type(nt, 0), done(nt, 0); // type 1 = corner, 2 = centre
    for (uint32_t seed = 0; seed < nt; ++seed) {
        if (done[seed]) continue;
        bool solved = false;
        for (int option = 0; option < 4 && !solved; ++option) {
            std::vector<uint8_t> lab = label, ty = type;
            const glm::uvec3& st = m.triangles[seed];
            bool ok = true;
            for (int c = 0; c < 3; ++c) {
                const uint8_t want = option == c ? kO : kE;
                if (lab[st[c]] != kUnset && lab[st[c]] != want) ok = false;
                lab[st[c]] = want;
            }
            ty[seed] = option < 3 ? 1 : 2;
            std::vector<uint32_t> queue{seed}, comp;
            std::vector<uint8_t> seen(nt, 0);
            seen[seed] = 1;
            while (!queue.empty() && ok) {
                const uint32_t t = queue.back();
                queue.pop_back();
                comp.push_back(t);
                const glm::uvec3& tri = m.triangles[t];
                for (uint32_t i = w->polyOffsets[t]; i < w->polyOffsets[t + 1] && ok; ++i) {
                    const glm::uvec2 ef = w->edgeFaces[w->polyEdges[i]];
                    const uint32_t q = ef.x == t ? ef.y : ef.x;
                    if (q == kNone) continue;
                    const glm::uvec2 ed = w->edges[w->polyEdges[i]];
                    const uint8_t la = lab[ed.x], lb = lab[ed.y];
                    const glm::uvec3& qt = m.triangles[q];
                    uint32_t third = qt.x;
                    for (int c = 0; c < 3; ++c)
                        if (qt[c] != ed.x && qt[c] != ed.y) third = qt[c];
                    uint8_t qtype, thirdLabel;
                    if (la == kO || lb == kO) {
                        qtype = 1;
                        thirdLabel = kE;
                    } else {
                        qtype = ty[t] == 1 ? 2 : 1;
                        thirdLabel = qtype == 1 ? kO : kE;
                    }
                    (void)tri;
                    if (ty[q] && ty[q] != qtype) ok = false;
                    ty[q] = qtype;
                    if (lab[third] != kUnset && lab[third] != thirdLabel) ok = false;
                    lab[third] = thirdLabel;
                    if (!seen[q]) {
                        seen[q] = 1;
                        queue.push_back(q);
                    }
                }
            }
            if (!ok) continue;
            label = std::move(lab);
            type = std::move(ty);
            for (uint32_t t : comp) done[t] = 1;
            solved = true;
        }
        if (!solved) {
            error = "The mesh is not a regular Loop subdivision (no consistent corner / edge pattern)";
            return false;
        }
    }
    std::vector<uint32_t> coarseIndex(nv, kNone);
    for (uint32_t v = 0; v < nv; ++v) {
        if (label[v] == kUnset) {
            error = "The mesh has loose vertices";
            return false;
        }
        if (label[v] == kO) {
            coarseIndex[v] = uint32_t(out.vertexChild.size());
            out.vertexChild.push_back(v);
        }
    }
    // The two O neighbours of every E vertex (from the corner triangles).
    std::vector<glm::uvec2> ends(nv, glm::uvec2(kNone));
    auto addEnd = [&](uint32_t e, uint32_t o) {
        if (ends[e].x == o || ends[e].y == o) return true;
        if (ends[e].x == kNone) ends[e].x = o;
        else if (ends[e].y == kNone) ends[e].y = o;
        else return false;
        return true;
    };
    for (uint32_t t = 0; t < nt; ++t) {
        if (type[t] != 1) continue;
        const glm::uvec3& tri = m.triangles[t];
        for (int c = 0; c < 3; ++c)
            if (label[tri[c]] == kO && (!addEnd(tri[(c + 1) % 3], tri[c]) || !addEnd(tri[(c + 2) % 3], tri[c]))) {
                error = "The mesh is not a regular subdivision (an edge point has more than two ends)";
                return false;
            }
    }
    for (uint32_t t = 0; t < nt; ++t) {
        if (type[t] != 2) continue;
        const glm::uvec3& e = m.triangles[t];
        auto common = [&](uint32_t a, uint32_t b) {
            for (uint32_t x : {ends[a].x, ends[a].y})
                if (x != kNone && (x == ends[b].x || x == ends[b].y)) return x;
            return kNone;
        };
        const uint32_t c1 = common(e[0], e[1]), c2 = common(e[1], e[2]), c0 = common(e[2], e[0]);
        if (c0 == kNone || c1 == kNone || c2 == kNone) {
            error = "The mesh is not a regular subdivision (a centre triangle has no corners)";
            return false;
        }
        const uint32_t a = coarseIndex[c0], b = coarseIndex[c1], c = coarseIndex[c2];
        const uint32_t pts[3] = {e[0], e[1], e[2]};
        const uint64_t keys[3] = {pairKey(a, b), pairKey(b, c), pairKey(c, a)};
        for (int i = 0; i < 3; ++i) {
            auto [it, inserted] = out.edgePoint.try_emplace(keys[i], pts[i]);
            if (!inserted && it->second != pts[i]) {
                error = "The mesh is not a regular subdivision (edge points disagree)";
                return false;
            }
        }
        out.coarse.triangles.push_back({a, b, c});
    }
    return true;
}

} // namespace

bool subdivReconstruct(Mesh& mesh, std::string& error, BVH* bvh) {
    if (mesh.multires) {
        if (!subdivCommit(mesh)) {
            // dropped: reconstruct from this mesh alone
        } else if (mesh.multires->current != 0) {
            error = "Reconstruct Subdiv works at the lowest level (step down to SDiv 1 first)";
            return false;
        }
    }
    if (mesh.triangleCount() == 0) {
        error = "Nothing to reconstruct";
        return false;
    }
    Reconstruction rec;
    const bool quads = mesh.quadCount() > 0;
    if (!(quads ? reconstructCatmullClark(mesh, rec, error) : reconstructLoop(mesh, rec, error))) return false;
    const uint32_t nc = uint32_t(rec.vertexChild.size());
    if (nc < 3 || rec.coarse.triangles.empty()) {
        error = "The mesh is too small to have a lower level";
        return false;
    }
    Mesh& coarse = rec.coarse;
    coarse.positions.resize(nc);
    for (uint32_t v = 0; v < nc; ++v) coarse.positions[v] = mesh.positions[rec.vertexChild[v]];
    std::shared_ptr<SubdivTopology> tp = buildCoarse(coarse, rec.pairing, rec.catmullClark, true);
    tp->vertexChild = rec.vertexChild;
    tp->faceChild = rec.catmullClark ? rec.faceChild : std::vector<uint32_t>{};
    tp->edgeChild.resize(tp->edges.size());
    for (size_t e = 0; e < tp->edges.size(); ++e) {
        auto it = rec.edgePoint.find(pairKey(tp->edges[e].x, tp->edges[e].y));
        if (it == rec.edgePoint.end()) {
            error = "The mesh is not a regular subdivision (an edge has no edge point)";
            return false;
        }
        tp->edgeChild[e] = it->second;
    }
    if (tp->fineVertices != mesh.vertexCount()) {
        error = "The mesh is not a regular subdivision (vertex counts don't add up)";
        return false;
    }
    if (!buildFineMetadata(*tp, coarse, mesh.triangles, error)) return false;
    // Expected fine triangle count: 2n per n-gon (Catmull-Clark) or 4 per triangle (Loop).
    size_t expected = 0;
    for (size_t p = 0; p < tp->polygonCount(); ++p) expected += tp->catmullClark ? 2 * polySize(*tp, p) : 4;
    if (expected != mesh.triangleCount()) {
        error = "The mesh is not a regular subdivision (triangle counts don't add up)";
        return false;
    }

    // Coarse shape: the fine level's corner vertices, as ZBrush does (fitting the cage so its subdivision passes
    // through them overshoots badly on sharp shapes, and compounds over several levels); the detail of the fine
    // level keeps it exactly as it was.
    const std::vector<glm::vec3>& fineBase = baseChannel(mesh);
    for (uint32_t v = 0; v < nc; ++v) coarse.positions[v] = fineBase[rec.vertexChild[v]];
    // Attributes: everything copied down from the fine level.
    std::vector<uint8_t> f;
    if (mesh.hasMask()) restrictVertices(*tp, mesh.mask, coarse.mask, 0.0f, nullptr, f);
    if (mesh.hasColors()) restrictVertices(*tp, mesh.colors, coarse.colors, glm::vec3(1.0f), nullptr, f);
    if (mesh.hasPolygroups()) restrictFaces(*tp, mesh.polygroups, coarse.polygroups, coarse.triangleCount(), 0u, nullptr, f);
    if (mesh.hasHidden()) restrictFaces(*tp, mesh.hidden, coarse.hidden, coarse.triangleCount(), uint8_t(0), nullptr, f);
    if (mesh.hasUvs()) restrictUvs(*tp, coarse, mesh.uvs, coarse.uvs, nullptr, f);
    if (!rec.pairing.empty()) coarse.quadPartners = rec.pairing;
    if (!mesh.paintLayers.empty()) {
        restrictVertices(*tp, mesh.paintBase, coarse.paintBase, glm::vec3(1.0f), nullptr, f);
        for (const PaintLayer& l : mesh.paintLayers) {
            PaintLayer nl = l;
            nl.color.clear();
            nl.alpha.clear();
            restrictVertices(*tp, l.color, nl.color, glm::vec3(1.0f), nullptr, f);
            restrictVertices(*tp, l.alpha, nl.alpha, 0.0f, nullptr, f);
            coarse.paintLayers.push_back(std::move(nl));
        }
        coarse.activePaintLayer = mesh.activePaintLayer;
        compositePaintAll(coarse);
    }
    SubdivLevel fineLevel;
    fineLevel.topo = tp;
    if (!mesh.layers.empty()) {
        coarse.base = coarse.positions;
        for (const SculptLayer& l : mesh.layers) {
            SculptLayer cl{l.name, std::vector<glm::vec3>(nc), l.weight};
            for (uint32_t v = 0; v < nc; ++v) cl.delta[v] = l.delta[rec.vertexChild[v]];
            const std::vector<glm::vec3> s = subdivideField(*tp, true, cl.delta);
            auto d = std::make_shared<std::vector<glm::vec3>>(s.size());
            for (size_t v = 0; v < s.size(); ++v) (*d)[v] = l.delta[v] - s[v];
            fineLevel.layerDetail.push_back(std::move(d));
            coarse.layers.push_back(std::move(cl));
        }
        evaluateLayers(coarse);
    }
    {
        const std::vector<glm::vec3> Q = subdivideField(*tp, true, baseChannel(coarse));
        fineLevel.detail = std::make_shared<std::vector<glm::vec3>>();
        fitDetail(*tp, mesh.triangles, Q, fineBase, *fineLevel.detail);
    }
    MultiresStack& s = ensureStack(mesh);
    fineLevel.data = s.levels[0].data;
    fineLevel.stale = false;
    s.levels[0] = std::move(fineLevel);
    s.levels.insert(s.levels.begin(), SubdivLevel{std::make_shared<Mesh>(std::move(coarse)), nullptr, nullptr, {}, false});
    s.current = 0;
    enterLevel(mesh, 0, bvh, s.levels[1].data);
    return true;
}

// --- Undo ---

size_t SubdivUndo::bytes() const {
    // Only what this record alone keeps alive (shared levels are counted by whoever else holds them).
    if (!stack) return plain && plain.use_count() == 1 ? plain->positions.size() * 64 : 0;
    size_t total = 0;
    for (const SubdivLevel& l : stack->levels) {
        if (l.data && l.data.use_count() == 1) total += l.data->positions.size() * 40 + l.data->triangles.size() * 20;
        if (l.detail && l.detail.use_count() == 1) total += l.detail->size() * sizeof(glm::vec3);
    }
    return total;
}

void applySubdivUndo(SubdivUndo& r, Mesh& mesh, BVH& bvh) {
    subdivCommit(mesh);
    std::shared_ptr<Mesh> leaving = mesh.multires ? mesh.multires->levels[size_t(mesh.multires->current)].data : nullptr;
    std::shared_ptr<SubdivLevelCache> cache = mesh.multires ? mesh.multires->cache : nullptr;
    std::swap(mesh.multires, r.stack);
    if (mesh.multires) {
        if (!mesh.multires->cache) mesh.multires->cache = std::move(cache);
        enterLevel(mesh, mesh.multires->current, &bvh, std::move(leaving));
        return;
    } else if (r.plain) {
        std::shared_ptr<PaintTexture> texture = mesh.texture;
        const bool toTexture = mesh.paintToTexture;
        mesh = leanCopy(*r.plain);
        mesh.texture = std::move(texture);
        mesh.paintToTexture = toTexture;
        mesh.rebuildTopology();
        mesh.recomputeAllNormals();
        if (hasPaintLayers(mesh)) compositePaintAll(mesh);
    }
    mesh.topologyDirty = true;
    bvh.build(mesh);
}

namespace {

// Runs a level operation on the active SubTool as one undo step that swaps the level stack back.
// `plainLevel`: for operations that can start without levels, the level of the new stack that holds the mesh
// as it was (it is the undo's `plain` mesh, shared rather than copied).
template <class Op>
bool runUndoable(Document& doc, const std::string& label, Op op, std::string& message, int plainLevel = -1) {
    Mesh& mesh = doc.mesh;
    subdivCommit(mesh);
    auto rec = std::make_unique<SubdivUndo>();
    rec->stack = mesh.multires; // holding it makes the operation copy what it changes
    const bool hadStack = bool(rec->stack);
    const int fromLevel = subdivCurrentLevel(mesh);
    if (!hadStack && plainLevel < 0) return false;
    if (!op(mesh, message, &doc.bvh)) return false;
    if (!hadStack) rec->plain = mesh.multires->levels[size_t(plainLevel)].data;
    else if (!mesh.multires) rec->plain = rec->stack->levels[size_t(fromLevel)].data; // the level that remained
    doc.bvhDirty = false;
    UndoRecord r;
    r.label = label;
    r.subdiv = std::move(rec);
    doc.undo.push(std::move(r));
    if (doc.activeLayer >= int(mesh.layers.size())) doc.activeLayer = -1;
    return true;
}

std::string levelSummary(const Mesh& m) {
    std::string text = subdivLevelText(m);
    return (text.empty() ? std::string("No subdivision levels") : text) + ", " + std::to_string(m.triangleCount()) + " triangles";
}

} // namespace

bool divideUndoable(Document& doc, bool smooth, std::string& message) {
    return runUndoable(doc, "Divide", [&](Mesh& m, std::string& msg, BVH* bvh) {
        if (!subdivDivide(m, smooth, msg, bvh)) return false;
        msg = "Divided: " + levelSummary(m);
        return true;
    }, message, doc.mesh.multires ? -1 : 0);
}

bool setSubdivLevelUndoable(Document& doc, int level, std::string& message) {
    if (!doc.mesh.multires) {
        message = "This SubTool has no subdivision levels (Divide adds them)";
        return false;
    }
    if (level == subdivCurrentLevel(doc.mesh)) {
        message = levelSummary(doc.mesh);
        return true;
    }
    return runUndoable(doc, "SDiv " + std::to_string(level + 1), [&](Mesh& m, std::string& msg, BVH* bvh) {
        if (!subdivSetLevel(m, level, msg, bvh)) return false;
        msg = levelSummary(m);
        return true;
    }, message);
}

bool deleteLowerUndoable(Document& doc, std::string& message) {
    return runUndoable(doc, "Del Lower", [&](Mesh& m, std::string& msg, BVH*) {
        if (!subdivDeleteLower(m, msg)) return false;
        msg = "Deleted the lower levels: " + levelSummary(m);
        return true;
    }, message);
}

bool deleteHigherUndoable(Document& doc, std::string& message) {
    return runUndoable(doc, "Del Higher", [&](Mesh& m, std::string& msg, BVH*) {
        if (!subdivDeleteHigher(m, msg)) return false;
        msg = "Deleted the higher levels: " + levelSummary(m);
        return true;
    }, message);
}

bool reconstructUndoable(Document& doc, std::string& message) {
    return runUndoable(doc, "Reconstruct Subdiv", [&](Mesh& m, std::string& msg, BVH* bvh) {
        if (!subdivReconstruct(m, msg, bvh)) return false;
        msg = "Reconstructed a lower level: " + levelSummary(m);
        return true;
    }, message, doc.mesh.multires ? -1 : 1);
}

} // namespace sf
