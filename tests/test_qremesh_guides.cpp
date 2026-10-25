// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh Guides (ZBrush's ZRemesher Guides): a curve drawn round an "eye" on a sphere gets a closed edge loop
// along it, and the edges near the curve line up with its tangent much better than without the guide; drawing
// (snapping, closing, mirroring), Delete Last / Clear, commands, SubTool swaps and project round trips.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/QRemesh.h"
#include "core/QRemeshGuides.h"
#include "io/ObjIO.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

using namespace sf;

namespace {

constexpr float kPi = 3.14159265f;

// A circle of angular radius `angle` on the unit sphere around direction `c`.
std::vector<glm::vec3> circleOnSphere(glm::vec3 c, float angle, int n, float radius = 1.0f) {
    c = glm::normalize(c);
    const glm::vec3 u = glm::normalize(glm::cross(c, std::abs(c.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    const glm::vec3 v = glm::cross(c, u);
    std::vector<glm::vec3> out;
    for (int i = 0; i <= n; ++i) { // the last point repeats the first: a drawn loop
        const float phi = 2.0f * kPi * float(i % n) / float(n);
        out.push_back(radius * (std::cos(angle) * c + std::sin(angle) * (std::cos(phi) * u + std::sin(phi) * v)));
    }
    return out;
}

// Unique edges of the result as exported (quads once).
std::vector<std::pair<uint32_t, uint32_t>> faceEdges(const Mesh& m) {
    std::set<std::pair<uint32_t, uint32_t>> edges;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        glm::uvec4 f;
        const int n = m.exportFace(t, f);
        for (int k = 0; k < n; ++k) {
            const uint32_t a = f[k], b = f[(k + 1) % n];
            edges.insert({std::min(a, b), std::max(a, b)});
        }
    }
    return {edges.begin(), edges.end()};
}

struct GuideFit {
    float meanAngle = 90.0f; // degrees, edges near the curve against its tangent (0 = along or across it)
    size_t nearEdges = 0;
    bool closedLoop = false; // a cycle of edges runs all the way round on the curve
    float loopGap = 360.0f;  // the largest gap (degrees round the circle) in that cycle's coverage
};

// How well the edges of `m` near the circle (around `c` at `angle`, on the unit sphere) follow it.
GuideFit measureFit(const Mesh& m, glm::vec3 c, float angle) {
    c = glm::normalize(c);
    const glm::vec3 u = glm::normalize(glm::cross(c, std::abs(c.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    const glm::vec3 v = glm::cross(c, u);
    const auto edges = faceEdges(m);
    double len = 0.0;
    for (auto [a, b] : edges) len += glm::length(m.positions[a] - m.positions[b]);
    const float edge = float(len / double(edges.size()));
    // Distance of a point from the circle, and the circle's tangent nearest it.
    auto onCircle = [&](glm::vec3 p, glm::vec3& tangent, float& phi) {
        const glm::vec3 d = glm::normalize(p);
        phi = std::atan2(glm::dot(d, v), glm::dot(d, u));
        const glm::vec3 radial = std::cos(phi) * u + std::sin(phi) * v;
        tangent = -std::sin(phi) * u + std::cos(phi) * v;
        const glm::vec3 q = std::cos(angle) * c + std::sin(angle) * radial;
        return glm::length(p - q);
    };
    GuideFit fit;
    double sum = 0.0;
    std::map<uint32_t, std::vector<uint32_t>> loop; // edges on the curve, along it
    for (auto [a, b] : edges) {
        const glm::vec3 pa = m.positions[a], pb = m.positions[b];
        glm::vec3 t;
        float phi;
        if (onCircle(0.5f * (pa + pb), t, phi) > 0.5f * edge) continue;
        const glm::vec3 d = glm::normalize(pb - pa);
        float deg = std::acos(std::clamp(std::abs(glm::dot(d, t)), 0.0f, 1.0f)) * 180.0f / kPi; // 0..90
        deg = std::min(deg, 90.0f - deg); // a cross field: across the curve is as good as along it
        sum += deg;
        ++fit.nearEdges;
        glm::vec3 ta, tb;
        float fa, fb;
        if (deg < 20.0f && std::acos(std::clamp(std::abs(glm::dot(d, t)), 0.0f, 1.0f)) < 0.5f &&
            onCircle(pa, ta, fa) < 0.3f * edge && onCircle(pb, tb, fb) < 0.3f * edge) {
            loop[a].push_back(b);
            loop[b].push_back(a);
        }
    }
    fit.meanAngle = fit.nearEdges ? float(sum / double(fit.nearEdges)) : 90.0f;
    // The biggest connected set of loop edges: a cycle (every vertex twice) going all the way round.
    std::set<uint32_t> seen;
    for (auto& [start, nb] : loop) {
        if (seen.count(start)) continue;
        std::vector<uint32_t> stack{start}, comp;
        seen.insert(start);
        while (!stack.empty()) {
            const uint32_t x = stack.back();
            stack.pop_back();
            comp.push_back(x);
            for (uint32_t y : loop[x])
                if (seen.insert(y).second) stack.push_back(y);
        }
        bool cycle = comp.size() >= 6;
        std::vector<float> phis;
        for (uint32_t x : comp) {
            cycle &= loop[x].size() == 2;
            glm::vec3 t;
            float phi;
            onCircle(m.positions[x], t, phi);
            phis.push_back(phi * 180.0f / kPi);
        }
        std::sort(phis.begin(), phis.end());
        float gap = 360.0f - (phis.back() - phis.front());
        for (size_t i = 1; i < phis.size(); ++i) gap = std::max(gap, phis[i] - phis[i - 1]);
        if (std::getenv("SF_GUIDE_DEBUG")) {
            size_t d1 = 0, d2 = 0, d3 = 0;
            for (uint32_t x : comp) (loop[x].size() == 1 ? d1 : loop[x].size() == 2 ? d2 : d3)++;
            std::printf("    component %zu vertices: degree 1 %zu, 2 %zu, 3+ %zu, gap %.0f\n", comp.size(), d1, d2, d3, gap);
        }
        if (gap < fit.loopGap) fit.loopGap = gap;
        if (cycle && gap < 30.0f) fit.closedLoop = true;
    }
    return fit;
}

QRemeshGuide guideFrom(const std::vector<glm::vec3>& points, bool closed) {
    QRemeshGuide g;
    g.points = points;
    if (closed) g.points.pop_back();
    g.closed = closed;
    g.stroke = 1;
    return g;
}

} // namespace

TEST(qremesh_guide_loop_round_an_eye) {
    const Mesh sphere = makeIcosphere(6);
    const glm::vec3 eye(0.35f, 0.3f, 0.88f); // off every axis, so the plain field has no reason to circle it
    const float angle = 0.42f;                // about 24 degrees
    const std::vector<glm::vec3> circle = circleOnSphere(eye, angle, 240);
    QRemeshOptions o;
    o.targetQuads = 3000;
    o.adaptive = 0.0f;
    Mesh plain, guided;
    std::string err;
    CHECK(qremesh(sphere, o, plain, err));
    o.guides = {guideFrom(circle, true)};
    QRemeshStats st;
    CHECK(qremesh(sphere, o, guided, err, {}, &st));
    CHECK(st.guides == 1);
    const TopologyReport topo = topologyReport(guided);
    CHECK(topo.manifold() && topo.boundaryEdges == 0 && topo.quadFraction() >= 0.95f);
    CHECK(st.quads > 2500 && st.quads < 3500);
    if (const char* dir = std::getenv("SF_GUIDE_DEBUG")) {
        // Faces the circle passes through in their own polygroup, for --polygroups renders.
        for (Mesh* m : {&plain, &guided}) {
            Mesh copy = *m;
            copy.ensurePolygroups();
            for (uint32_t t = 0; t < copy.triangleCount(); ++t) {
                const glm::uvec3 tri = copy.triangles[t];
                float lo = 1e9f, hi = -1e9f;
                for (int k = 0; k < 3; ++k) {
                    const float d = glm::dot(glm::normalize(copy.positions[tri[k]]), glm::normalize(eye)) - std::cos(angle);
                    lo = std::min(lo, d), hi = std::max(hi, d);
                }
                copy.polygroups[t] = lo <= 0.0f && hi >= 0.0f ? 1u : 0u;
            }
            saveObj(std::string(dir) + (m == &plain ? "/guide_plain.obj" : "/guide_guided.obj"), copy, err);
        }
    }
    const GuideFit a = measureFit(plain, eye, angle), b = measureFit(guided, eye, angle);
    std::printf("  without guide: %.1f deg mean over %zu edges, loop %d (gap %.0f deg)\n", a.meanAngle, a.nearEdges,
                int(a.closedLoop), a.loopGap);
    std::printf("  with guide:    %.1f deg mean over %zu edges, loop %d (gap %.0f deg), %zu quads\n", b.meanAngle,
                b.nearEdges, int(b.closedLoop), b.loopGap, st.quads);
    CHECK(b.meanAngle < 0.5f * a.meanAngle);
    CHECK(b.meanAngle < 8.0f);
    CHECK(b.closedLoop);
    CHECK(!a.closedLoop);
    // Deterministic.
    Mesh again;
    CHECK(qremesh(sphere, o, again, err));
    CHECK(again.positions == guided.positions && again.triangles == guided.triangles);
}
