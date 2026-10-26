// CRKW sculpt helper: independent surface smoothing and curvature sharpening.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/CkwPolish.h"
#include "core/Commands.h"
#include "core/ThreadPool.h"
#include <algorithm>
#include <cmath>

namespace sf {
void registerCkwPolishCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"ckw_clay_polish", "CRKW Clay Polish: smooth and sharpen visible, unmasked geometry (one undo step).",
        {{"softness", T::Number, "Surface smoothing strength, 0-1. Default 0.3.", false},
         {"soft_passes", T::Number, "Smoothing passes, 0-20. Default 2.", false},
         {"sharpness", T::Number, "Curvature sharpening strength, 0-1. Default 0.4.", false},
         {"sharp_passes", T::Number, "Sharpening passes, 0-20. Default 2.", false},
         {"preserve_shape", T::Number, "HC smoothing shape correction, 0-1. Default 0.7.", false},
         {"edge_smoothing", T::Number, "Tangential smoothing during sharpening, 0-1. Default 0.35.", false}},
        [](Document& doc, const Args& args) -> CommandResult {
            Mesh& m = doc.mesh;
            if (m.vertexCount() == 0) return {false, "CRKW Clay Polish: select a mesh first"};
            if (doc.sculptor.dyntopo.enabled) return {false, "CRKW Clay Polish: disable Dynamic topology first"};
            const float layerWeight = m.layerTargetWeight(doc.activeLayer);
            if (!std::isfinite(layerWeight) || std::abs(layerWeight) < 1e-6f)
                return {false, "CRKW Clay Polish: the active sculpt layer must have nonzero weight"};
            auto number = [&](const char* key, double fallback, double hi) {
                const double v = argNumber(args, key, fallback);
                return float(std::clamp(std::isfinite(v) ? v : fallback, 0.0, hi));
            };
            const float softness = number("softness", .3, 1), sharpness = number("sharpness", .4, 1);
            const int softPasses = int(number("soft_passes", 2, 20)), sharpPasses = int(number("sharp_passes", 2, 20));
            const float preserve = number("preserve_shape", .7, 1), edge = number("edge_smoothing", .35, 1);
            if ((softness == 0 || softPasses == 0) && (sharpness == 0 || sharpPasses == 0))
                return {true, "CRKW Clay Polish: no passes enabled"};
            const size_t count = m.vertexCount();
            // Jacobi passes read a fixed input, so thread scheduling cannot change the result.
            const auto original = m.positions;
            std::vector<glm::vec3> next(count), correction(count);
            std::vector<float> influence(count);
            poolFor(count, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v)
                    influence[v] = m.vertexHidden(uint32_t(v)) ? 0.0f : std::clamp(1.0f - m.maskAt(uint32_t(v)), 0.0f, 1.0f);
            });
            auto average = [&](const auto& values, uint32_t v) {
                const auto ring = m.neighbors(v);
                glm::vec3 avg(0);
                for (uint32_t n : ring) avg += values[n];
                return ring.empty() ? values[v] : avg / float(ring.size());
            };
            for (int pass = 0; pass < softPasses && softness > 0; ++pass) {
                poolFor(count, [&](size_t b, size_t e) {
                    for (size_t v = b; v < e; ++v) {
                        const auto p = m.positions[v];
                        next[v] = p + softness * (average(m.positions, uint32_t(v)) - p);
                        correction[v] = next[v] - glm::mix(p, original[v], preserve);
                    }
                });
                poolFor(count, [&](size_t b, size_t e) {
                    for (size_t v = b; v < e; ++v) {
                        const auto corrected = next[v] - preserve * .5f *
                            (correction[v] + average(correction, uint32_t(v)));
                        next[v] = glm::mix(m.positions[v], corrected, influence[v]);
                    }
                });
                m.positions.swap(next);
            }
            for (int pass = 0; pass < sharpPasses && sharpness > 0; ++pass) {
                m.recomputeAllNormals();
                poolFor(count, [&](size_t b, size_t e) {
                    for (size_t v = b; v < e; ++v) {
                        const auto p = m.positions[v], n = m.normals[v];
                        const auto lap = average(m.positions, uint32_t(v)) - p;
                        const auto normal = n * glm::dot(lap, n);
                        auto delta = sharpness * (-.5f * normal + edge * (lap - normal));
                        float length = 0;
                        const auto ring = m.neighbors(uint32_t(v));
                        for (uint32_t other : ring) length += glm::length(m.positions[other] - p);
                        const float limit = ring.empty() ? 0 : .25f * length / float(ring.size());
                        const float d = glm::length(delta);
                        if (d > limit && d > 0) delta *= limit / d;
                        next[v] = p + influence[v] * delta;
                    }
                });
                m.positions.swap(next);
            }
            UndoRecord undo;
            undo.label = "CRKW Clay Polish";
            undo.layer = doc.activeLayer;
            for (const auto& p : m.positions) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
                    m.positions = original;
                    m.recomputeAllNormals();
                    return {false, "CRKW Clay Polish: non-finite geometry; operation cancelled"};
                }
            }
            auto& target = m.layerTarget(doc.activeLayer);
            for (uint32_t v = 0; v < count; ++v) {
                const auto delta = m.positions[v] - original[v];
                if (delta == glm::vec3(0)) continue;
                // No layers: target aliases positions; save the original position directly.
                if (m.layers.empty()) undo.positions.add(v, original[v]);
                else {
                    undo.positions.add(v, target[v]);
                    target[v] += delta / layerWeight;
                }
                m.markDirty(v);
            }
            if (undo.positions.empty()) return {true, "CRKW Clay Polish: surface unchanged"};
            m.recomputeAllNormals();
            doc.bvhDirty = true;
            doc.undo.push(std::move(undo));
            return {true, "CRKW Clay Polish applied"};
        }});
    // Retain the original command for existing scripts while exposing the new brand.
    Command alias = *registry.find("ckw_clay_polish");
    alias.name = "crkw_clay_polish";
    registry.add(std::move(alias));
}
} // namespace sf
