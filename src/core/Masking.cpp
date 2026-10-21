// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Masking.h"

#include "core/Commands.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace sf {

namespace {

// Applies `next` (old mask -> new value of vertex v) to every visible vertex,
// double-buffered so each pass reads the previous one.
template <class F>
bool maskPass(Mesh& mesh, F next) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    std::vector<float>& mask = mesh.ensureMask();
    std::vector<float> out(mask);
    const bool anyHidden = mesh.hasHidden();
    bool changed = false;
    for (uint32_t v = 0; v < n; ++v) {
        if (anyHidden && mesh.vertexHidden(v)) continue;
        const float m = std::clamp(next(mask, v), 0.0f, 1.0f);
        if (m != mask[v]) {
            out[v] = m;
            changed = true;
        }
    }
    if (changed) {
        mask.swap(out);
        mesh.markAllDirty();
    }
    return changed;
}

float neighbourAverage(const Mesh& mesh, const std::vector<float>& mask, uint32_t v) {
    auto ring = mesh.neighbors(v);
    if (ring.empty()) return mask[v];
    float sum = 0.0f;
    for (uint32_t u : ring) sum += mask[u];
    return sum / static_cast<float>(ring.size());
}

template <class F>
bool repeatPass(Mesh& mesh, int iterations, F next) {
    bool changed = false;
    for (int i = 0; i < std::max(1, iterations); ++i) {
        if (!maskPass(mesh, next)) break;
        changed = true;
    }
    return changed;
}

float smoothstep01(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / std::max(e1 - e0, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

bool maskClear(Mesh& mesh) {
    if (!mesh.hasMask()) return false;
    const bool changed = std::any_of(mesh.mask.begin(), mesh.mask.end(), [](float m) { return m != 0.0f; });
    mesh.clearMask();
    mesh.markAllDirty();
    return changed;
}

bool maskAll(Mesh& mesh) {
    return maskPass(mesh, [](const std::vector<float>&, uint32_t) { return 1.0f; });
}

bool maskInvert(Mesh& mesh) {
    return maskPass(mesh, [](const std::vector<float>& m, uint32_t v) { return 1.0f - m[v]; });
}

bool maskBlur(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        return 0.5f * m[v] + 0.5f * neighbourAverage(mesh, m, v);
    });
}

bool maskSharpen(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        // Unsharp step (away from the neighbour average) plus a mild contrast boost around 0.5.
        float s = m[v] + (m[v] - neighbourAverage(mesh, m, v));
        return (s - 0.5f) * 1.25f + 0.5f;
    });
}

bool maskGrow(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        float r = m[v];
        for (uint32_t u : mesh.neighbors(v)) r = std::max(r, m[u]);
        return r;
    });
}

bool maskShrink(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        float r = m[v];
        for (uint32_t u : mesh.neighbors(v)) r = std::min(r, m[u]);
        return r;
    });
}

float vertexConcavity(const Mesh& mesh, uint32_t v) {
    auto ring = mesh.neighbors(v);
    if (ring.empty()) return 0.0f;
    const glm::vec3& p = mesh.positions[v];
    glm::vec3 avg(0.0f);
    float edge = 0.0f;
    for (uint32_t u : ring) {
        avg += mesh.positions[u];
        edge += glm::distance(p, mesh.positions[u]);
    }
    avg /= static_cast<float>(ring.size());
    edge /= static_cast<float>(ring.size());
    if (edge <= 1e-12f) return 0.0f;
    return glm::dot(mesh.normals[v], avg - p) / edge;
}

bool maskByCavity(Mesh& mesh, float strength, float threshold) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    if (n == 0) return false;
    strength = std::clamp(strength, 0.0f, 1.0f);
    // One smoothing pass over the raw concavity keeps single noisy vertices out.
    std::vector<float> raw(n), cav(n);
    for (uint32_t v = 0; v < n; ++v) raw[v] = vertexConcavity(mesh, v);
    for (uint32_t v = 0; v < n; ++v) cav[v] = 0.5f * raw[v] + 0.5f * neighbourAverage(mesh, raw, v);
    constexpr float kRamp = 0.15f; // concavity above the threshold at which the mask is full
    return maskPass(mesh, [&](const std::vector<float>&, uint32_t v) {
        return strength * smoothstep01(threshold, threshold + kRamp, cav[v]);
    });
}

bool maskByPolygroup(Mesh& mesh, uint32_t group, bool add) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    if (n == 0) return false;
    std::vector<uint8_t> inGroup(n, 0);
    bool found = false;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        if (mesh.polygroupAt(t) != group || mesh.triangleHidden(t)) continue;
        const glm::uvec3& tri = mesh.triangles[t];
        inGroup[tri.x] = inGroup[tri.y] = inGroup[tri.z] = 1;
        found = true;
    }
    if (!found && add) return false;
    return maskPass(mesh, [&](const std::vector<float>& m, uint32_t v) {
        return inGroup[v] ? 1.0f : add ? m[v] : 0.0f;
    });
}

void Sculptor::dabMask(Mesh& mesh, const BVH& bvh, const DabParams& p, Symmetry sym) {
    if (!active_) return;
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    if (s <= 0.0f) return;
    std::vector<float>& mask = mesh.ensureMask();
    for (const glm::vec3& mirror : mirrors(sym)) {
        const glm::vec3 center = p.center * mirror, viewDir = p.viewDir * mirror;
        brushVertices(mesh, bvh, center, p.radius, verts_);
        for (uint32_t v : verts_) {
            if (p.frontFacesOnly && glm::dot(mesh.normals[v], viewDir) > 0.0f) continue;
            const float a = s * brushFalloff(glm::distance(mesh.positions[v], center), p.radius);
            if (a <= 0.0f) continue;
            const float m = p.invert ? mask[v] * (1.0f - a) : mask[v] + (1.0f - mask[v]) * a;
            if (m == mask[v]) continue;
            recordMask(mesh, v);
            mask[v] = m;
            mesh.markDirty(v);
        }
    }
}

void registerMaskingCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    // Wraps a mask operation as an undoable command.
    auto add = [&registry](std::string name, std::string label, std::string description, std::vector<ParamSpec> params,
                           std::function<bool(Mesh&, const Args&)> op) {
        registry.add({std::move(name), std::move(description), std::move(params),
                      [label, op](Document& doc, const Args& a) -> CommandResult {
                          if (doc.mesh.vertexCount() == 0) return {false, "The mesh is empty"};
                          UndoRecord record = UndoRecord::captureMask(doc.mesh, label);
                          if (!op(doc.mesh, a)) return {true, label + ": mask unchanged"};
                          doc.undo.push(std::move(record));
                          doc.modified = true;
                          return {true, label};
                      }});
    };
    const ParamSpec iterations{"iterations", T::Number, "Number of passes (1-100). Default 1.", false};
    auto iters = [](const Args& a) { return std::clamp(int(argNumber(a, "iterations", 1)), 1, 100); };

    add("mask_clear", "Clear Mask", "Remove the mask so every vertex can be sculpted again.", {},
        [](Mesh& m, const Args&) { return maskClear(m); });
    add("mask_all", "Mask All", "Mask every visible vertex (protects the whole mesh from brushes).", {},
        [](Mesh& m, const Args&) { return maskAll(m); });
    add("mask_invert", "Invert Mask", "Invert the mask of visible vertices: masked becomes free and vice versa.", {},
        [](Mesh& m, const Args&) { return maskInvert(m); });
    add("mask_blur", "Blur Mask", "Soften the mask edges by averaging with neighbouring vertices.", {iterations},
        [iters](Mesh& m, const Args& a) { return maskBlur(m, iters(a)); });
    add("mask_sharpen", "Sharpen Mask", "Tighten soft mask edges, pushing values towards fully masked or free.",
        {iterations}, [iters](Mesh& m, const Args& a) { return maskSharpen(m, iters(a)); });
    add("mask_grow", "Grow Mask", "Expand the masked region by one edge ring per iteration.", {iterations},
        [iters](Mesh& m, const Args& a) { return maskGrow(m, iters(a)); });
    add("mask_shrink", "Shrink Mask", "Contract the masked region by one edge ring per iteration.", {iterations},
        [iters](Mesh& m, const Args& a) { return maskShrink(m, iters(a)); });
    add("mask_by_cavity", "Mask by Cavity",
        "Mask concave areas (creases, folds, pores), replacing the current mask - e.g. to protect detail while smoothing.",
        {{"strength", T::Number, "Mask value in the deepest cavities (0-1). Default 1.", false},
         {"threshold", T::Number,
          "Concavity above which vertices get masked (-1 to 1; 0 = anything concave, higher = only deep creases). "
          "Default 0.02.",
          false}},
        [](Mesh& m, const Args& a) {
            return maskByCavity(m, float(argNumber(a, "strength", 1.0)),
                                std::clamp(float(argNumber(a, "threshold", 0.02)), -1.0f, 1.0f));
        });
    add("mask_by_polygroup", "Mask by Polygroup",
        "Mask the vertices of one polygroup (meshes without polygroups are all group 0).",
        {{"index", T::Number, "Polygroup id.", true},
         {"add", T::Bool, "Add to the current mask instead of replacing it. Default false.", false}},
        [](Mesh& m, const Args& a) {
            return maskByPolygroup(m, uint32_t(std::max(0.0, argNumber(a, "index", 0))), argBool(a, "add", false));
        });
}

} // namespace sf
