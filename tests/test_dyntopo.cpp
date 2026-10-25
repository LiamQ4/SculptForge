// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dynamic topology (core/Dyntopo.h): strokes remesh the surface under the brush and leave a valid mesh
// (manifold, no degenerate or flipped triangles, Euler characteristic kept, adjacency and BVH matching a fresh
// build) after every dab; symmetry makes mirrored topology; undo and redo restore exactly; attributes are
// carried; boundaries, polygroup borders, masks and subdivision levels are respected.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Multires.h"
#include "core/PathStroke.h"
#include "core/Primitives.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

using namespace sf;

namespace {

// Why `m` isn't a valid mesh ("" if it is): corners in range and distinct, no degenerate triangles, every
// vertex used, every edge on one or two triangles wound oppositely (closed: exactly two), the adjacency lists
// as a rebuild makes them, and for a closed mesh V - E + F == euler.
std::string invalid(const Mesh& m, bool closed, int euler = 2) {
    const size_t nv = m.vertexCount(), nt = m.triangleCount();
    std::vector<int> used(nv, 0);
    std::map<std::pair<uint32_t, uint32_t>, int> directed;
    for (size_t t = 0; t < nt; ++t) {
        const glm::uvec3 tri = m.triangles[t];
        for (int c = 0; c < 3; ++c)
            if (tri[c] >= nv) return "corner out of range";
        if (tri.x == tri.y || tri.y == tri.z || tri.x == tri.z) return "repeated corner";
        const glm::vec3 n = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
        if (!(glm::length(n) > 1e-12f)) return "degenerate triangle " + std::to_string(t);
        for (int c = 0; c < 3; ++c) {
            used[tri[c]] = 1;
            if (++directed[{tri[c], tri[(c + 1) % 3]}] > 1) return "edge used twice in one direction";
        }
    }
    for (size_t v = 0; v < nv; ++v)
        if (!used[v]) return "unused vertex " + std::to_string(v);
    size_t edges = 0;
    for (const auto& [e, n] : directed) {
        const bool twin = directed.count({e.second, e.first}) > 0;
        if (closed && !twin) return "open edge";
        if (!twin || e.first < e.second) ++edges;
    }
    if (closed && long(nv) - long(edges) + long(nt) != euler) return "Euler characteristic changed";
    Mesh fresh = m;
    fresh.rebuildTopology();
    for (uint32_t v = 0; v < nv; ++v) {
        const IndexRing a = m.vertexTriangles(v), b = fresh.vertexTriangles(v);
        if (!std::equal(a.begin(), a.end(), b.begin(), b.end())) return "triangle list of vertex " + std::to_string(v);
        const IndexRing c = m.neighbors(v), d = fresh.neighbors(v);
        if (!std::equal(c.begin(), c.end(), d.begin(), d.end())) return "ring of vertex " + std::to_string(v);
    }
    if (m.normals.size() != nv) return "normals";
    return {};
}

// The BVH finds what a brute-force search finds (vertex queries and ray casts) at a few spots.
bool bvhMatches(const Mesh& m, const BVH& bvh) {
    for (int i = 0; i < 12; ++i) {
        const glm::vec3 c = m.positions[(size_t(i) * 7919) % m.vertexCount()];
        const float r = 0.3f;
        std::vector<uint32_t> got, want;
        bvh.queryVertices(m, c, r, got);
        for (uint32_t v = 0; v < m.vertexCount(); ++v)
            if (glm::dot(m.positions[v] - c, m.positions[v] - c) <= r * r) want.push_back(v);
        if (got != want) return false;
        const glm::vec3 origin = c * 3.0f + glm::vec3(0.01f, 0.02f, 0.0f);
        const glm::vec3 dir = glm::normalize(-origin);
        RayHit hit;
        const bool found = bvh.raycast(m, origin, dir, hit);
        float best = 1e30f;
        for (const glm::uvec3& t : m.triangles) {
            const glm::vec3 a = m.positions[t.x], e1 = m.positions[t.y] - a, e2 = m.positions[t.z] - a;
            const glm::vec3 pv = glm::cross(dir, e2);
            const float det = glm::dot(e1, pv);
            if (std::abs(det) < 1e-12f) continue;
            const glm::vec3 s = origin - a;
            const float u = glm::dot(s, pv) / det;
            const glm::vec3 q = glm::cross(s, e1);
            const float w = glm::dot(dir, q) / det, tt = glm::dot(e2, q) / det;
            if (u >= 0 && w >= 0 && u + w <= 1 && tt > 0) best = std::min(best, tt);
        }
        if (found != (best < 1e30f) || (found && std::abs(hit.t - best) > 1e-4f)) return false;
    }
    return true;
}

// A dab where a ray from `from` towards the origin hits the surface.
bool dabToward(Document& d, glm::vec3 from, float radius, BrushType type, Symmetry sym, float strength = 0.5f,
               glm::vec3 delta = glm::vec3(0.0f), bool invert = false) {
    RayHit hit;
    const glm::vec3 dir = glm::normalize(-from);
    if (!d.bvh.raycast(d.mesh, from, dir, hit)) return false;
    DabParams p;
    p.center = hit.position;
    p.viewDir = dir;
    p.radius = radius;
    p.strength = strength;
    p.strokeDelta = delta;
    p.invert = invert;
    d.sculptor.dab(d.mesh, d.bvh, type, p, sym);
    return true;
}

Document sphereDoc(int subdivisions = 3) {
    Document d = makeDoc(makeIcosphere(subdivisions, 1.0f));
    d.sculptor.dyntopo.enabled = true;
    return d;
}

// Mirror image of every vertex across x = 0 within `tol` (nearest, both ways), or empty when some vertex has none.
std::vector<uint32_t> mirrorMap(const Mesh& m, float tol) {
    std::vector<uint32_t> map(m.vertexCount(), ~0u);
    auto nearest = [&](const glm::vec3& p) {
        uint32_t best = ~0u;
        float bd = tol * tol;
        for (uint32_t w = 0; w < m.vertexCount(); ++w) {
            const glm::vec3 d = m.positions[w] - p;
            if (glm::dot(d, d) < bd) {
                bd = glm::dot(d, d);
                best = w;
            }
        }
        return best;
    };
    const glm::vec3 s(-1.0f, 1.0f, 1.0f);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const uint32_t w = nearest(m.positions[v] * s);
        if (w == ~0u || nearest(m.positions[w] * s) != v) {
            std::printf("  no mirror image for vertex %u at %f %f %f\n", v, m.positions[v].x, m.positions[v].y, m.positions[v].z);
            return {};
        }
        map[v] = w;
    }
    return map;
}

// Every triangle's mirror image (reversed winding) is a triangle too.
bool mirroredTopology(const Mesh& m, const std::vector<uint32_t>& map) {
    std::set<std::tuple<uint32_t, uint32_t, uint32_t>> tris;
    auto canon = [](uint32_t a, uint32_t b, uint32_t c) {
        // Rotation starting at the smallest corner.
        if (b < a && b < c) return std::make_tuple(b, c, a);
        if (c < a && c < b) return std::make_tuple(c, a, b);
        return std::make_tuple(a, b, c);
    };
    for (const glm::uvec3& t : m.triangles) tris.insert(canon(t.x, t.y, t.z));
    int bad = 0;
    for (const glm::uvec3& t : m.triangles)
        if (!tris.count(canon(map[t.x], map[t.z], map[t.y]))) {
            if (bad++ < 4) {
                const glm::vec3 c = (m.positions[t.x] + m.positions[t.y] + m.positions[t.z]) / 3.0f;
                std::printf("  unmirrored triangle at %f %f %f\n", c.x, c.y, c.z);
            }
        }
    return bad == 0;
}

} // namespace

TEST(dyntopo_edge_length_follows_brush_or_mesh) {
    DyntopoSettings s;
    CHECK(std::abs(dyntopoEdgeLength(s, 0.1f, 2.0f) - 0.1f * 0.4f * std::pow(0.1f, 0.5f)) < 1e-6f);
    s.detail = 1.0f;
    const float fine = dyntopoEdgeLength(s, 0.1f, 2.0f);
    s.detail = 0.0f;
    CHECK(dyntopoEdgeLength(s, 0.1f, 2.0f) > fine * 9.0f);
    CHECK(dyntopoEdgeLength(s, 0.2f, 2.0f) > dyntopoEdgeLength(s, 0.1f, 2.0f) * 1.9f); // follows the brush
    s.constantDetail = true;
    CHECK(dyntopoEdgeLength(s, 0.2f, 2.0f) == dyntopoEdgeLength(s, 0.1f, 2.0f)); // follows the mesh
    CHECK(std::abs(dyntopoEdgeLength(s, 0.1f, 2.0f) - 0.1f) < 1e-6f);             // diagonal / 20
}

TEST(dyntopo_adds_detail_under_the_brush_only) {
    Document d = sphereDoc(2); // 320 triangles, edges ~0.3
    const size_t before = d.mesh.triangleCount();
    const glm::vec3 far = d.mesh.positions[0];
    d.sculptor.dyntopo.detail = 0.8f;
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
    for (int i = 0; i < 6; ++i) CHECK(dabToward(d, glm::vec3(0.1f * float(i), 0.2f, 3.0f), 0.35f, BrushType::Standard, Symmetry{false, false, false}));
    CHECK(invalid(d.mesh, true).empty());
    CHECK(bvhMatches(d.mesh, d.bvh));
    UndoRecord r = d.sculptor.endStroke();
    CHECK(r.topology != nullptr);
    CHECK(d.mesh.triangleCount() > before * 2);
    CHECK(invalid(d.mesh, true).empty());
    CHECK(!d.mesh.dynamicAdjacency());
    // Edges near the stroke are about the detail size, the far side kept its coarse triangles.
    const float target = dyntopoEdgeLength(d.sculptor.dyntopo, 0.35f, 2.0f);
    float longestNear = 0.0f;
    for (const glm::uvec3& t : d.mesh.triangles)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 a = d.mesh.positions[t[c]], b = d.mesh.positions[t[(c + 1) % 3]];
            if (glm::distance((a + b) * 0.5f, glm::vec3(0.0f, 0.1f, 1.0f)) < 0.15f) longestNear = std::max(longestNear, glm::distance(a, b));
        }
    CHECK(longestNear <= target * 4.0f / 3.0f * 1.25f);
    CHECK(d.mesh.positions[0] == far);
}

TEST(dyntopo_random_strokes_stay_valid) {
    Document d = sphereDoc(3);
    uint32_t rng = 12345;
    auto rnd = [&rng] {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return float(rng & 0xffff) / 65535.0f;
    };
    const BrushType types[] = {BrushType::Standard, BrushType::SnakeHook, BrushType::Smooth, BrushType::Clay,
                               BrushType::Pinch, BrushType::Inflate, BrushType::Crease, BrushType::Flatten};
    bool allValid = true, bvhOk = true;
    std::string why;
    for (int stroke = 0; stroke < 24; ++stroke) {
        const BrushType type = types[stroke % 8];
        d.sculptor.dyntopo.detail = rnd();
        d.sculptor.dyntopo.constantDetail = stroke % 5 == 4;
        const Symmetry sym{stroke % 3 == 0, false, stroke % 7 == 0};
        CHECK(d.sculptor.beginStroke(d.mesh, brushName(type)));
        glm::vec3 dir = glm::normalize(glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) + glm::vec3(0.0f, 0.0f, 0.01f));
        const glm::vec3 step = glm::normalize(glm::cross(dir, glm::vec3(0.3f, 1.0f, 0.1f))) * 0.08f;
        const float radius = 0.15f + 0.35f * rnd();
        for (int i = 0; i < 8; ++i) {
            dabToward(d, (dir + step * float(i)) * 3.0f, radius, type, sym, 0.6f, step * 0.5f, rnd() < 0.3f);
            if (allValid) {
                why = invalid(d.mesh, true);
                allValid = why.empty();
                if (!allValid) std::printf("  stroke %d dab %d: %s\n", stroke, i, why.c_str());
            }
        }
        bvhOk = bvhOk && bvhMatches(d.mesh, d.bvh);
        d.undo.push(d.sculptor.endStroke());
        if (allValid) {
            why = invalid(d.mesh, true);
            allValid = why.empty();
            if (!allValid) std::printf("  after stroke %d: %s\n", stroke, why.c_str());
        }
    }
    CHECK(allValid);
    CHECK(bvhOk);
    // Undo all the way back gives the sphere again, exactly.
    const Mesh sphere = makeIcosphere(3, 1.0f);
    while (d.undo.canUndo()) d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.triangles == sphere.triangles);
    CHECK(d.mesh.positions == sphere.positions);
    CHECK(bvhMatches(d.mesh, d.bvh));
}

TEST(dyntopo_undo_redo_restore_exactly) {
    Document d = sphereDoc(3);
    d.mesh.ensureMask();
    d.mesh.ensureColors(glm::vec3(0.2f, 0.5f, 0.9f));
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.mask[v] = d.mesh.positions[v].y > 0.5f ? 0.5f : 0.0f;
    const Mesh before = d.mesh;
    d.sculptor.dyntopo.detail = 0.7f;
    for (int s = 0; s < 3; ++s) {
        CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
        for (int i = 0; i < 6; ++i) dabToward(d, glm::vec3(0.15f * float(i) - 0.4f, 0.3f * float(s), 3.0f), 0.3f, BrushType::Standard, Symmetry{});
        d.undo.push(d.sculptor.endStroke());
    }
    const Mesh after = d.mesh;
    CHECK(after.triangleCount() != before.triangleCount());
    while (d.undo.canUndo()) d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.triangles == before.triangles);
    CHECK(d.mesh.positions == before.positions);
    CHECK(d.mesh.mask == before.mask);
    CHECK(d.mesh.colors == before.colors);
    CHECK(invalid(d.mesh, true).empty());
    while (d.undo.canRedo()) d.undo.redo(d.mesh, d.bvh);
    CHECK(d.mesh.triangles == after.triangles);
    CHECK(d.mesh.positions == after.positions);
    CHECK(d.mesh.mask == after.mask);
    CHECK(invalid(d.mesh, true).empty());
    CHECK(bvhMatches(d.mesh, d.bvh));
}

TEST(dyntopo_x_symmetry_makes_mirrored_topology) {
    Document d = sphereDoc(3);
    d.sculptor.dyntopo.detail = 0.75f;
    const Symmetry sym{true, false, false};
    // Strokes on one side, across the middle and pulling with Snake Hook.
    for (int s = 0; s < 3; ++s) {
        const BrushType type = s == 2 ? BrushType::SnakeHook : BrushType::Standard;
        CHECK(d.sculptor.beginStroke(d.mesh, brushName(type)));
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 from = s == 0 ? glm::vec3(0.5f, 0.1f * float(i) - 0.3f, 1.0f)
                                 : s == 1 ? glm::vec3(0.1f * float(i) - 0.35f, -0.2f, 1.0f)
                                          : glm::vec3(0.6f, 0.4f, 0.8f);
            dabToward(d, from * 3.0f, 0.3f, type, sym, 0.5f, s == 2 ? glm::vec3(0.02f, 0.02f, 0.03f) : glm::vec3(0.0f));
        }
        d.undo.push(d.sculptor.endStroke());
        CHECK(invalid(d.mesh, true).empty());
        const std::vector<uint32_t> map = mirrorMap(d.mesh, dyntopoEdgeLength(d.sculptor.dyntopo, 0.3f, 2.0f) * 0.3f);
        CHECK(map.size() == d.mesh.vertexCount() && mirroredTopology(d.mesh, map));
        CHECK(d.sculptor.dynamicTopology().stats.unpaired == 0);
    }
    CHECK(d.mesh.triangleCount() > 1280 * 2);
    const float target = dyntopoEdgeLength(d.sculptor.dyntopo, 0.3f, 2.0f);
    const std::vector<uint32_t> map = mirrorMap(d.mesh, target * 0.3f);
    CHECK(map.size() == d.mesh.vertexCount());
    if (map.size() == d.mesh.vertexCount()) CHECK(mirroredTopology(d.mesh, map));
}

TEST(dyntopo_carries_attributes_and_drops_uvs) {
    Document d = sphereDoc(2);
    Mesh& m = d.mesh;
    m.ensureColors();
    m.ensurePolygroups();
    m.uvs.assign(m.triangleCount() * 3, glm::vec2(0.5f));
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.colors[v] = glm::vec3(m.positions[v].x * 0.5f + 0.5f, 0.2f, 0.1f);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        m.polygroups[t] = (m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]).y > 0.0f ? 7u : 3u;
    }
    d.activeLayer = m.addLayer("Detail");
    m.rebuildTopology();
    d.bvh.build(m);
    // Group border vertices, and the border's length and each group's area (base positions).
    auto borderVerts = [](const Mesh& mesh) {
        std::set<uint32_t> out;
        for (uint32_t v = 0; v < mesh.vertexCount(); ++v) {
            std::set<uint32_t> groups;
            for (uint32_t t : mesh.vertexTriangles(v)) groups.insert(mesh.polygroups[t]);
            if (groups.size() > 1) out.insert(v);
        }
        return out;
    };
    auto borderLength = [](const Mesh& mesh) {
        std::map<std::pair<uint32_t, uint32_t>, std::set<uint32_t>> groups;
        for (uint32_t t = 0; t < mesh.triangleCount(); ++t)
            for (int c = 0; c < 3; ++c) {
                const uint32_t a = mesh.triangles[t][c], b = mesh.triangles[t][(c + 1) % 3];
                groups[{std::min(a, b), std::max(a, b)}].insert(mesh.polygroups[t]);
            }
        float len = 0.0f;
        for (const auto& [e, g] : groups)
            if (g.size() > 1) len += glm::distance(mesh.base[e.first], mesh.base[e.second]);
        return len;
    };
    auto groupArea = [](const Mesh& mesh, uint32_t group) {
        float area = 0.0f;
        for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
            if (mesh.polygroups[t] != group) continue;
            const glm::uvec3 tri = mesh.triangles[t];
            area += 0.5f * glm::length(glm::cross(mesh.base[tri.y] - mesh.base[tri.x], mesh.base[tri.z] - mesh.base[tri.x]));
        }
        return area;
    };
    const float lengthBefore = borderLength(m), area7 = groupArea(m, 7), area3 = groupArea(m, 3);
    const size_t borderBefore = borderVerts(m).size();
    d.sculptor.dyntopo.detail = 0.9f;
    CHECK(d.sculptor.beginStroke(m, "Standard", d.activeLayer));
    for (int i = 0; i < 8; ++i) dabToward(d, glm::vec3(0.1f * float(i) - 0.4f, 0.05f, 3.0f), 0.35f, BrushType::Standard, Symmetry{});
    d.undo.push(d.sculptor.endStroke());
    CHECK(invalid(m, true).empty());
    CHECK(m.uvs.empty());
    CHECK(m.colors.size() == m.vertexCount());
    CHECK(m.polygroups.size() == m.triangleCount());
    CHECK(m.layers[0].delta.size() == m.vertexCount() && m.base.size() == m.vertexCount());
    // Interpolated colours: red follows x everywhere.
    bool colorsOk = true;
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        colorsOk = colorsOk && std::abs(m.colors[v].x - (m.positions[v].x * 0.5f + 0.5f)) < 0.12f && m.colors[v].y == 0.2f;
    CHECK(colorsOk);
    // Positions are still base + layer offsets, and the stroke went into the layer.
    bool layered = true;
    float moved = 0.0f;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        layered = layered && glm::distance(m.positions[v], m.base[v] + m.layers[0].delta[v]) < 1e-5f;
        moved = std::max(moved, glm::length(m.layers[0].delta[v]));
    }
    CHECK(layered);
    CHECK(moved > 0.0f);
    // The border is still there, now with more vertices along it, and both groups stayed in their halves.
    CHECK(borderVerts(m).size() > borderBefore);
    CHECK(std::abs(borderLength(m) - lengthBefore) < 0.1f * lengthBefore);
    CHECK(std::abs(groupArea(m, 7) - area7) < 0.05f * area7);
    CHECK(std::abs(groupArea(m, 3) - area3) < 0.05f * area3);
    d.undo.undo(m, d.bvh);
    CHECK(m.uvs.size() == m.triangleCount() * 3);
    CHECK(borderVerts(m).size() == borderBefore);
}

TEST(dyntopo_keeps_boundaries_and_masked_areas) {
    Document d = makeDoc(makePlane(8, 2.0f)); // y = 0, x and z in -1..1
    d.sculptor.dyntopo.enabled = true;
    d.sculptor.dyntopo.detail = 0.8f;
    Mesh& m = d.mesh;
    m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].x < -0.3f ? 1.0f : 0.0f;
    const Mesh before = m;
    CHECK(d.sculptor.beginStroke(m, "Standard"));
    for (int i = 0; i < 10; ++i) {
        RayHit hit;
        const glm::vec3 from(0.2f * float(i) - 1.0f, 3.0f, 0.9f);
        CHECK(d.bvh.raycast(m, from, glm::vec3(0, -1, 0), hit));
        DabParams p;
        p.center = hit.position;
        p.viewDir = glm::vec3(0, -1, 0);
        p.radius = 0.4f;
        p.strength = 0.3f;
        d.sculptor.dab(m, d.bvh, BrushType::Standard, p, Symmetry{false, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
    CHECK(invalid(m, false).empty());
    CHECK(m.triangleCount() > before.triangleCount());
    // Boundary vertices stay on the square's edges; the edge at z = 1 got new vertices along it.
    size_t onEdge = 0;
    bool boundaryOk = true;
    std::map<std::pair<uint32_t, uint32_t>, int> count;
    for (const glm::uvec3& t : m.triangles)
        for (int c = 0; c < 3; ++c) ++count[{std::min(t[c], t[(c + 1) % 3]), std::max(t[c], t[(c + 1) % 3])}];
    for (const auto& [e, n] : count)
        if (n == 1)
            for (uint32_t v : {e.first, e.second}) {
                const glm::vec3 p = m.positions[v];
                const bool edge = std::abs(std::abs(p.x) - 1.0f) < 2e-3f || std::abs(std::abs(p.z) - 1.0f) < 2e-3f;
                boundaryOk = boundaryOk && edge;
                onEdge += std::abs(p.z - 1.0f) < 2e-3f;
            }
    CHECK(boundaryOk);
    CHECK(onEdge / 2 > 9); // more than the 9 vertices it started with (each counted on two boundary edges)
    // The fully masked part kept its vertices and triangles exactly.
    auto maskedTriangles = [](const Mesh& mesh) {
        std::multiset<std::vector<float>> out;
        for (const glm::uvec3& t : mesh.triangles) {
            if (mesh.mask[t.x] < 1.0f || mesh.mask[t.y] < 1.0f || mesh.mask[t.z] < 1.0f) continue;
            std::vector<float> key;
            for (int c = 0; c < 3; ++c)
                for (int k = 0; k < 3; ++k) key.push_back(mesh.positions[t[c]][k]);
            out.insert(key);
        }
        return out;
    };
    CHECK(!maskedTriangles(before).empty());
    CHECK(maskedTriangles(before) == maskedTriangles(m));
}

TEST(dyntopo_refuses_subdivision_levels) {
    Document d = sphereDoc(1);
    CHECK(dyntopoBlocker(d.mesh).empty());
    CommandRegistry reg;
    CHECK(reg.execute(d, "subdiv_divide", {}).ok);
    CHECK(!dyntopoBlocker(d.mesh).empty());
    const size_t before = d.mesh.triangleCount();
    d.sculptor.dyntopo.enabled = true;
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
    dabToward(d, glm::vec3(0, 0, 3), 0.3f, BrushType::Standard, Symmetry{});
    UndoRecord r = d.sculptor.endStroke();
    CHECK(!r.topology);
    CHECK(d.mesh.triangleCount() == before);
}

TEST(dyntopo_is_deterministic_with_any_thread_count) {
    auto run = [](bool serial) {
        ThreadPool::shared().setSerial(serial);
        Document d = sphereDoc(3);
        d.sculptor.dyntopo.detail = 0.8f;
        CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
        for (int i = 0; i < 10; ++i) dabToward(d, glm::vec3(0.1f * float(i) - 0.5f, 0.2f, 3.0f), 0.4f, BrushType::Clay, Symmetry{true, false, false});
        d.undo.push(d.sculptor.endStroke());
        ThreadPool::shared().setSerial(false);
        return d.mesh;
    };
    const Mesh a = run(true), b = run(false);
    CHECK(a.triangles == b.triangles);
    CHECK(a.positions == b.positions);
}

TEST(dyntopo_smooth_simplifies) {
    Document d = sphereDoc(4); // 5120 triangles
    const size_t before = d.mesh.triangleCount();
    d.sculptor.dyntopo.detail = 0.0f;
    CHECK(d.sculptor.beginStroke(d.mesh, "Smooth"));
    for (int i = 0; i < 10; ++i) dabToward(d, glm::vec3(0.05f * float(i), 0.0f, 3.0f), 0.5f, BrushType::Smooth, Symmetry{});
    d.undo.push(d.sculptor.endStroke());
    CHECK(invalid(d.mesh, true).empty());
    CHECK(d.mesh.triangleCount() < before);
}
