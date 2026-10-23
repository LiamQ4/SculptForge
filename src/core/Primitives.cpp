// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Primitives.h"

#include <cmath>
#include <numbers>
#include <type_traits>
#include <unordered_map>

namespace sf {

namespace {

uint64_t edgeKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return (uint64_t(a) << 32) | b;
}

void finalize(Mesh& m) {
    m.rebuildTopology();
    m.recomputeAllNormals();
}

} // namespace

Mesh makeIcosphere(int subdivisions, float radius) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    Mesh m;
    m.positions = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                   {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    m.triangles = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                   {11, 10, 2}, {10, 7, 6}, {7, 1, 8},   {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
                   {3, 8, 9},  {4, 9, 5},  {2, 4, 11},  {6, 2, 10}, {8, 6, 7},   {9, 8, 1}};
    for (auto& p : m.positions) p = glm::normalize(p);

    for (int level = 0; level < subdivisions; ++level) {
        std::unordered_map<uint64_t, uint32_t> mids;
        mids.reserve(m.triangles.size() * 2);
        auto mid = [&](uint32_t a, uint32_t b) {
            auto [it, inserted] = mids.try_emplace(edgeKey(a, b), uint32_t(m.positions.size()));
            if (inserted) m.positions.push_back(glm::normalize(m.positions[a] + m.positions[b]));
            return it->second;
        };
        std::vector<glm::uvec3> tris;
        tris.reserve(m.triangles.size() * 4);
        for (const auto& tri : m.triangles) {
            uint32_t ab = mid(tri.x, tri.y), bc = mid(tri.y, tri.z), ca = mid(tri.z, tri.x);
            tris.push_back({tri.x, ab, ca});
            tris.push_back({tri.y, bc, ab});
            tris.push_back({tri.z, ca, bc});
            tris.push_back({ab, bc, ca});
        }
        m.triangles = std::move(tris);
    }
    for (auto& p : m.positions) p *= radius;
    finalize(m);
    return m;
}

Mesh makePlane(int resolution, float size) {
    Mesh m;
    const int n = std::max(resolution, 1);
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            m.positions.emplace_back((float(i) / n - 0.5f) * size, 0.0f, (float(j) / n - 0.5f) * size);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            uint32_t a = j * (n + 1) + i, b = a + 1, c = a + (n + 1), d = c + 1;
            m.triangles.push_back({a, c, b});
            m.triangles.push_back({b, c, d});
            m.pairQuad(uint32_t(m.triangles.size() - 2), uint32_t(m.triangles.size() - 1));
        }
    }
    finalize(m);
    return m;
}

Mesh makeCube(int resolution, float size) {
    const int n = std::max(resolution, 1);
    const float h = size * 0.5f;
    Mesh raw;
    // Each side: a grid spanned by axes u, v from its corner, wound to face outwards.
    const glm::vec3 axes[6][3] = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}},
                                  {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},  {{0, -1, 0}, {0, 0, 1}, {-1, 0, 0}},
                                  {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {1, 0, 0}, {0, -1, 0}}};
    for (int f = 0; f < 6; ++f) {
        const glm::vec3 normal = axes[f][0], u = axes[f][2], v = axes[f][1];
        const uint32_t first = uint32_t(raw.positions.size());
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i)
                raw.positions.push_back(h * (normal + (2.0f * float(i) / float(n) - 1.0f) * u +
                                             (2.0f * float(j) / float(n) - 1.0f) * v));
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const uint32_t a = first + uint32_t(j * (n + 1) + i), b = a + 1, c = a + uint32_t(n + 1), d = c + 1;
                raw.triangles.push_back({a, c, b}); // (v x u) = outward normal on every side
                raw.triangles.push_back({b, c, d});
                raw.pairQuad(uint32_t(raw.triangles.size() - 2), uint32_t(raw.triangles.size() - 1));
                raw.polygroups.push_back(uint32_t(f + 1));
                raw.polygroups.push_back(uint32_t(f + 1));
            }
    }
    return weldImported(raw);
}

Mesh loopSubdivide(const Mesh& src) {
    const uint32_t nv = static_cast<uint32_t>(src.vertexCount());

    // Edge -> (new vertex index, use count, opposite vertices).
    struct EdgeInfo {
        uint32_t index;
        uint32_t count;
        uint32_t opp[2];
    };
    std::unordered_map<uint64_t, EdgeInfo> edges;
    edges.reserve(src.triangles.size() * 2);
    uint32_t next = nv;
    for (const auto& t : src.triangles) {
        for (int e = 0; e < 3; ++e) {
            uint32_t a = t[e], b = t[(e + 1) % 3], c = t[(e + 2) % 3];
            auto [it, inserted] = edges.try_emplace(edgeKey(a, b), EdgeInfo{next, 0, {c, c}});
            if (inserted) ++next;
            if (it->second.count < 2) it->second.opp[it->second.count] = c;
            ++it->second.count;
        }
    }

    // Boundary neighbours per vertex (edges used by exactly one triangle).
    std::vector<std::vector<uint32_t>> boundaryNbrs(nv);
    for (const auto& [key, e] : edges) {
        if (e.count == 1) {
            uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
            boundaryNbrs[a].push_back(b);
            boundaryNbrs[b].push_back(a);
        }
    }

    // Loop subdivision is linear, so the same stencils apply to positions and
    // to sculpt-layer offsets, keeping layers consistent across levels.
    auto subdivideAttribute = [&](const std::vector<glm::vec3>& P) {
        std::vector<glm::vec3> out(next);
        for (uint32_t v = 0; v < nv; ++v) {
            const auto& bn = boundaryNbrs[v];
            if (bn.size() == 2) {
                out[v] = 0.75f * P[v] + 0.125f * (P[bn[0]] + P[bn[1]]);
            } else if (!bn.empty()) {
                out[v] = P[v]; // corner / non-manifold: keep fixed
            } else {
                auto ring = src.neighbors(v);
                const float n = static_cast<float>(ring.size());
                if (n < 2) {
                    out[v] = P[v];
                    continue;
                }
                float c = 0.375f + 0.25f * std::cos(2.0f * std::numbers::pi_v<float> / n);
                float beta = (0.625f - c * c) / n;
                glm::vec3 sum(0.0f);
                for (uint32_t u : ring) sum += P[u];
                out[v] = (1.0f - n * beta) * P[v] + beta * sum;
            }
        }
        for (const auto& [key, e] : edges) {
            uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
            if (e.count == 2)
                out[e.index] = 0.375f * (P[a] + P[b]) + 0.125f * (P[e.opp[0]] + P[e.opp[1]]);
            else
                out[e.index] = 0.5f * (P[a] + P[b]);
        }
        return out;
    };

    Mesh out;
    out.positions = subdivideAttribute(src.positions);
    if (!src.layers.empty()) {
        out.base = subdivideAttribute(src.base);
        for (const auto& l : src.layers) out.layers.push_back({l.name, subdivideAttribute(l.delta), l.weight});
    }

    // Mask and polypaint are interpolated linearly (old vertices keep their
    // value, edge vertices take the average) so painted borders stay crisp.
    auto interpolateLinear = [&](const auto& A) {
        std::remove_cvref_t<decltype(A)> result(next);
        for (uint32_t v = 0; v < nv; ++v) result[v] = A[v];
        for (const auto& [key, e] : edges) result[e.index] = (A[uint32_t(key >> 32)] + A[uint32_t(key & 0xffffffffu)]) * 0.5f;
        return result;
    };
    if (src.mask.size() == nv) out.mask = interpolateLinear(src.mask);
    if (src.colors.size() == nv) out.colors = interpolateLinear(src.colors);
    if (!src.paintLayers.empty()) {
        std::vector<glm::uvec3> midpoints; // new vertex, parent a, parent b
        midpoints.reserve(edges.size());
        for (const auto& [key, e] : edges) midpoints.push_back({e.index, uint32_t(key >> 32), uint32_t(key & 0xffffffffu)});
        subdividePaintLayers(src, out, next, midpoints);
    }

    const size_t nt = src.triangles.size();
    const bool groups = src.polygroups.size() == nt, hidden = src.hidden.size() == nt, uvs = src.uvs.size() == nt * 3;
    out.triangles.reserve(nt * 4);
    for (size_t ti = 0; ti < nt; ++ti) {
        const auto& t = src.triangles[ti];
        uint32_t ab = edges.at(edgeKey(t.x, t.y)).index;
        uint32_t bc = edges.at(edgeKey(t.y, t.z)).index;
        uint32_t ca = edges.at(edgeKey(t.z, t.x)).index;
        out.triangles.push_back({t.x, ab, ca});
        out.triangles.push_back({t.y, bc, ab});
        out.triangles.push_back({t.z, ca, bc});
        out.triangles.push_back({ab, bc, ca});
        // Children inherit their parent's group and visibility.
        if (groups) out.polygroups.insert(out.polygroups.end(), 4, src.polygroups[ti]);
        if (hidden) out.hidden.insert(out.hidden.end(), 4, src.hidden[ti]);
        if (uvs) {
            const glm::vec2 ua = src.uvs[ti * 3], ub = src.uvs[ti * 3 + 1], uc = src.uvs[ti * 3 + 2];
            const glm::vec2 uab = (ua + ub) * 0.5f, ubc = (ub + uc) * 0.5f, uca = (uc + ua) * 0.5f;
            out.uvs.insert(out.uvs.end(), {ua, uab, uca, ub, ubc, uab, uc, uca, ubc, uab, ubc, uca});
        }
    }
    // A quad (t, u) splits into four quads: each child at a diagonal corner pairs with the partner's child at the
    // same corner, and the child at the far corner with its own centre triangle (children 0..2 sit at corners
    // x, y, z; child 3 is the centre).
    if (src.quadPartners.size() == nt) {
        for (uint32_t ti = 0; ti < nt; ++ti) {
            const uint32_t u = src.quadPartners[ti];
            const int e = u == Mesh::kNoQuad ? -1 : src.quadDiagonal(ti, u);
            if (e < 0) continue;
            const glm::uvec3& t = src.triangles[ti];
            out.pairQuad(ti * 4 + uint32_t((e + 2) % 3), ti * 4 + 3);
            for (int c : {e, (e + 1) % 3})
                for (uint32_t k = 0; k < 3; ++k)
                    if (src.triangles[u][k] == t[c]) out.pairQuad(ti * 4 + uint32_t(c), u * 4 + k);
        }
    }
    finalize(out);
    return out;
}

} // namespace sf
