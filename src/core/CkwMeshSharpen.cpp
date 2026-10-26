// CRKW Mesh Sharpen: a port of the "Mesh Sharpen (c)" Blender geometry-nodes filter that the CRKW Sculpt
// Helper add-on bakes (Croked Tools/Sculpt Helper). Curvature-weighted Laplacian smoothing: every vertex moves
// toward the average of its neighbours weighted by THEIR curvature, so soft areas are pulled into the creases --
// planes flatten, edges tighten.
//
// Matches the node group step for step, constants included:
//  - curvature is measured once, on the input surface (the weights never change between iterations);
//  - Mean: cotangent mean-curvature normal (cotangents clamped to +-100, sines floored at 1e-6) divided by
//    twice the mixed area, dotted with the average adjacent face normal; Curvedness: the angle between that
//    average and the vertex normal, mapped from its [min, max] over the mesh to [1e-8, max];
//  - both scaled by sqrt(mean corner area) so mesh density doesn't change the result, then
//    w = max(|c|^exponent, 1e-8);
//  - neighbours are the polygon edges: a quad's diagonal is not one (Blender smooths the untriangulated mesh);
//  - Jacobi passes p += step * (sum(w_n p_n) / sum(w_n) - p), boundary vertices pinned, then mixed with the
//    input by Blend.
// Works on the current subdivision level, like every sculpt edit. Masked and hidden vertices keep their place.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/CkwPolish.h"
#include "core/Commands.h"
#include "core/ThreadPool.h"
#include <algorithm>
#include <cmath>

namespace sf {
namespace {
// Cotangent of the angle at `o` between `x` and `y`, as the node group computes it.
float cotAt(const glm::vec3& o, const glm::vec3& x, const glm::vec3& y) {
    const glm::vec3 a = glm::normalize(x - o), b = glm::normalize(y - o);
    const float s = glm::length(glm::cross(a, b));
    return glm::dot(a, b) / (s > 1e-6f ? s : 1e-6f);
}

glm::vec3 safeNormalize(const glm::vec3& v) {
    const float l = glm::length(v);
    return l > 0.0f ? v / l : glm::vec3(0.0f);
}

bool finite(float f) { return std::isfinite(f); }

// Per-vertex curvature weights of the input surface (see the file comment).
std::vector<float> curvatureWeights(const Mesh& m, bool curvedness, float exponent) {
    const size_t count = m.vertexCount();
    std::vector<float> value(count, 0.0f), rootArea(count, 0.0f);
    poolFor(count, [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            glm::vec3 lap(0.0f), normals(0.0f);
            float area = 0.0f;
            int corners = 0;
            for (uint32_t t : m.vertexTriangles(uint32_t(v))) {
                const glm::uvec3 tri = m.triangles[t];
                const int k = tri[0] == v ? 0 : tri[1] == v ? 1 : 2;
                const glm::vec3 P = m.positions[v], A = m.positions[tri[(k + 1) % 3]], B = m.positions[tri[(k + 2) % 3]];
                const float cotB = cotAt(B, P, A), cotA = cotAt(A, P, B);
                lap += std::clamp(cotB, -100.0f, 100.0f) * (A - P);
                area += (cotA * glm::dot(B - P, B - P) + cotB * glm::dot(A - P, A - P)) / 8.0f;
                normals += safeNormalize(glm::cross(A - P, B - P));
                ++corners;
            }
            if (corners == 0) continue;
            const glm::vec3 avgNormal = normals / float(corners);
            if (curvedness) {
                value[v] = std::acos(std::clamp(glm::dot(avgNormal, m.normals[v]), -1.0f, 1.0f));
            } else if (area != 0.0f) {
                value[v] = glm::dot(lap / (2.0f * area), avgNormal); // the corner means cancel in the ratio
            }
            const float meanArea = area / float(corners);
            rootArea[v] = meanArea > 0.0f ? std::sqrt(meanArea) : 0.0f;
        }
    });
    if (curvedness && count > 0) {
        const auto [lo, hi] = std::minmax_element(value.begin(), value.end());
        const float from = *lo, to = *hi, range = to - from;
        for (float& c : value) c = range > 0.0f ? 1e-8f + (c - from) / range * (to - 1e-8f) : 1e-8f;
    }
    std::vector<float> weight(count);
    for (size_t v = 0; v < count; ++v) {
        const float c = std::abs(value[v] * rootArea[v]);
        const float w = std::pow(finite(c) ? c : 0.0f, exponent);
        weight[v] = finite(w) ? std::max(w, 1e-8f) : 1e-8f;
    }
    return weight;
}

// The far end of `v`'s quad diagonals (edges that are not polygon edges), at most a few.
int quadDiagonalsOf(const Mesh& m, uint32_t v, uint32_t* out, int capacity) {
    int n = 0;
    if (!m.hasQuads()) return 0;
    for (uint32_t t : m.vertexTriangles(v)) {
        const uint32_t u = m.quadPartnerAt(t);
        if (u == Mesh::kNoQuad) continue;
        const int e = m.quadDiagonal(t, u);
        if (e < 0) continue;
        const glm::uvec3 tri = m.triangles[t];
        const uint32_t a = tri[e], b = tri[(e + 1) % 3];
        const uint32_t other = a == v ? b : b == v ? a : ~0u;
        if (other != ~0u && n < capacity && std::find(out, out + n, other) == out + n) out[n++] = other;
    }
    return n;
}

bool isBoundary(const Mesh& m, uint32_t v) {
    for (uint32_t n : m.neighbors(v)) {
        int shared = 0;
        for (uint32_t t : m.vertexTriangles(v)) {
            const glm::uvec3 tri = m.triangles[t];
            shared += tri[0] == n || tri[1] == n || tri[2] == n;
        }
        if (shared == 1) return true;
    }
    return false;
}
} // namespace

void registerCrkwMeshSharpenCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"crkw_mesh_sharpen",
        "CRKW Mesh Sharpen: curvature-weighted smoothing that flattens planes and tightens creases, on the current "
        "subdivision level (one undo step). Port of the Sculpt Helper Blender add-on's Mesh Sharpen.",
        {{"iterations", T::Number, "Passes, 0-128. Default 18.", false},
         {"step", T::Number, "How far each pass moves a vertex toward its weighted neighbours, 0-1. Default 1.", false},
         {"blend", T::Number, "Mix between the input (0) and the result (1). Default 1.", false},
         {"exponent", T::Number, "Curvature power, 0-1: lower spreads the pull, higher focuses it on creases. Default 1.", false},
         {"type", T::String, "Curvature: mean (default) or curvedness.", false},
         {"pin_borders", T::Bool, "Keep open borders in place. Default true.", false}},
        [](Document& doc, const Args& args) -> CommandResult {
            Mesh& m = doc.mesh;
            if (m.vertexCount() == 0) return {false, "CRKW Mesh Sharpen: select a mesh first"};
            if (doc.sculptor.dyntopo.enabled) return {false, "CRKW Mesh Sharpen: disable Dynamic topology first"};
            const float layerWeight = m.layerTargetWeight(doc.activeLayer);
            if (!std::isfinite(layerWeight) || std::abs(layerWeight) < 1e-6f)
                return {false, "CRKW Mesh Sharpen: the active sculpt layer must have nonzero weight"};
            auto number = [&](const char* key, double fallback, double hi) {
                const double v = argNumber(args, key, fallback);
                return float(std::clamp(std::isfinite(v) ? v : fallback, 0.0, hi));
            };
            const int iterations = int(number("iterations", 18, 128));
            const float step = number("step", 1, 1), blend = number("blend", 1, 1), exponent = number("exponent", 1, 1);
            const std::string type = argString(args, "type", "mean");
            if (type != "mean" && type != "curvedness")
                return {false, "CRKW Mesh Sharpen: type must be mean or curvedness"};
            const bool pin = argBool(args, "pin_borders", true);
            if (iterations == 0 || step == 0 || blend == 0) return {true, "CRKW Mesh Sharpen: no passes enabled"};

            const size_t count = m.vertexCount();
            const auto original = m.positions;
            m.recomputeAllNormals();
            const std::vector<float> weight = curvatureWeights(m, type == "curvedness", exponent);

            // Each vertex's neighbours and their share of the weighted average, fixed for every pass.
            std::vector<uint32_t> offsets(count + 1, 0);
            std::vector<uint8_t> moves(count, 1);
            poolFor(count, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    uint32_t diagonals[8];
                    const int nd = quadDiagonalsOf(m, uint32_t(v), diagonals, 8);
                    uint32_t n = 0;
                    for (uint32_t other : m.neighbors(uint32_t(v)))
                        n += std::find(diagonals, diagonals + nd, other) == diagonals + nd;
                    offsets[v + 1] = n;
                    if (n == 0 || (pin && isBoundary(m, uint32_t(v)))) moves[v] = 0;
                }
            });
            for (size_t v = 0; v < count; ++v) offsets[v + 1] += offsets[v];
            std::vector<uint32_t> ring(offsets[count]);
            std::vector<float> share(offsets[count]);
            poolFor(count, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    uint32_t diagonals[8];
                    const int nd = quadDiagonalsOf(m, uint32_t(v), diagonals, 8);
                    uint32_t at = offsets[v];
                    double total = 0;
                    for (uint32_t other : m.neighbors(uint32_t(v))) {
                        if (std::find(diagonals, diagonals + nd, other) != diagonals + nd) continue;
                        ring[at] = other;
                        share[at] = weight[other];
                        total += weight[other];
                        ++at;
                    }
                    for (uint32_t i = offsets[v]; i < at; ++i) share[i] = float(share[i] / total);
                }
            });

            // Jacobi passes read a fixed input, so thread scheduling cannot change the result.
            std::vector<glm::vec3> next(count);
            for (int pass = 0; pass < iterations; ++pass) {
                poolFor(count, [&](size_t b, size_t e) {
                    for (size_t v = b; v < e; ++v) {
                        const glm::vec3 p = m.positions[v];
                        if (!moves[v]) { next[v] = p; continue; }
                        glm::vec3 target(0.0f);
                        for (uint32_t i = offsets[v]; i < offsets[v + 1]; ++i) target += share[i] * m.positions[ring[i]];
                        next[v] = p + step * (target - p);
                    }
                });
                m.positions.swap(next);
            }
            const std::vector<float> influence = crkwInfluence(m);
            poolFor(count, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v)
                    m.positions[v] = original[v] + (m.positions[v] - original[v]) * (blend * influence[v]);
            });
            return crkwCommitPositions(doc, original, layerWeight, "CRKW Mesh Sharpen");
        }});
}
} // namespace sf
