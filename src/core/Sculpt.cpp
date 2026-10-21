// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Sculpt.h"

#include "core/BrushAlpha.h"

#include <algorithm>
#include <cmath>

namespace sf {

const char* brushName(BrushType type) {
    switch (type) {
    case BrushType::Standard: return "Standard";
    case BrushType::Clay: return "Clay";
    case BrushType::Smooth: return "Smooth";
    case BrushType::Move: return "Move";
    case BrushType::Inflate: return "Inflate";
    case BrushType::Crease: return "Crease";
    case BrushType::Flatten: return "Flatten";
    case BrushType::Pinch: return "Pinch";
    case BrushType::DamStandard: return "DamStandard";
    case BrushType::ClayBuildup: return "ClayBuildup";
    case BrushType::SnakeHook: return "SnakeHook";
    case BrushType::Trim: return "Trim";
    case BrushType::Layer: return "Layer";
    case BrushType::Nudge: return "Nudge";
    default: return "?";
    }
}

float brushFalloff(float dist, float radius) {
    float t = std::clamp(1.0f - dist / radius, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void brushVertices(const Mesh& mesh, const BVH& bvh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) {
    bvh.queryVertices(mesh, center, radius, out);
    if (mesh.hasHidden()) std::erase_if(out, [&](uint32_t v) { return mesh.vertexHidden(v); });
}

namespace {

glm::vec3 projectOnPlane(const glm::vec3& v, const glm::vec3& n) { return v - n * glm::dot(v, n); }

// --- Procedural 3D patterns (deterministic, so texture sticks to the surface) ---

uint32_t hash3(int x, int y, int z) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}
float hashFloat(uint32_t h) { return float(h & 0xffffffu) / float(0xffffff); }

float valueNoise(const glm::vec3& p) {
    glm::vec3 i = glm::floor(p), f = p - i;
    glm::vec3 u = f * f * (3.0f - 2.0f * f);
    auto at = [&](int dx, int dy, int dz) {
        return hashFloat(hash3(int(i.x) + dx, int(i.y) + dy, int(i.z) + dz));
    };
    float x00 = glm::mix(at(0, 0, 0), at(1, 0, 0), u.x), x10 = glm::mix(at(0, 1, 0), at(1, 1, 0), u.x);
    float x01 = glm::mix(at(0, 0, 1), at(1, 0, 1), u.x), x11 = glm::mix(at(0, 1, 1), at(1, 1, 1), u.x);
    return glm::mix(glm::mix(x00, x10, u.y), glm::mix(x01, x11, u.y), u.z);
}

float fbm(const glm::vec3& p) {
    return (valueNoise(p) * 0.57f + valueNoise(p * 2.03f + 17.0f) * 0.29f + valueNoise(p * 4.01f + 31.0f) * 0.14f);
}

// Distances to the nearest and second-nearest jittered feature points.
void worley(const glm::vec3& p, float& f1, float& f2) {
    glm::vec3 c = glm::floor(p);
    f1 = f2 = 1e9f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int x = int(c.x) + dx, y = int(c.y) + dy, z = int(c.z) + dz;
                uint32_t h = hash3(x, y, z);
                glm::vec3 feature(float(x) + hashFloat(h), float(y) + hashFloat(h * 747796405u + 1u),
                                  float(z) + hashFloat(h * 2891336453u + 7u));
                float d = glm::distance(p, feature);
                if (d < f1) {
                    f2 = f1;
                    f1 = d;
                } else if (d < f2) {
                    f2 = d;
                }
            }
}

// Returns 0..1, where 1 means "full brush effect here".
float samplePattern(BrushRecipe::Pattern pattern, const glm::vec3& p) {
    switch (pattern) {
    case BrushRecipe::Pattern::Noise: return std::clamp(fbm(p) * 1.4f - 0.2f, 0.0f, 1.0f);
    case BrushRecipe::Pattern::Ridges: {
        float n = 1.0f - std::abs(2.0f * fbm(p) - 1.0f);
        return n * n * n;
    }
    case BrushRecipe::Pattern::Scales: {
        float f1, f2;
        worley(p, f1, f2);
        float t = std::clamp(f1 / std::max(f2, 1e-4f), 0.0f, 1.0f); // 0 at centre, 1 at the border
        return std::sqrt(std::max(0.0f, 1.0f - t * t));             // domed scale
    }
    case BrushRecipe::Pattern::Pores: {
        float f1, f2;
        worley(p, f1, f2);
        float t = std::clamp(f1 / 0.3f, 0.0f, 1.0f);
        return (1.0f - t) * (1.0f - t);
    }
    case BrushRecipe::Pattern::Cracks: {
        float f1, f2;
        worley(p, f1, f2);
        float t = std::clamp((f2 - f1) / 0.12f, 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }
    default: return 1.0f;
    }
}

} // namespace

std::vector<glm::vec3> Sculptor::mirrors(Symmetry sym) {
    std::vector<glm::vec3> out;
    for (int mask = 0; mask < 8; ++mask) {
        if ((mask & 1 && !sym.x) || (mask & 2 && !sym.y) || (mask & 4 && !sym.z)) continue;
        out.emplace_back(mask & 1 ? -1.0f : 1.0f, mask & 2 ? -1.0f : 1.0f, mask & 4 ? -1.0f : 1.0f);
    }
    return out;
}

bool Sculptor::beginStroke(const Mesh& mesh, std::string label, int layer) {
    if (layer >= static_cast<int>(mesh.layers.size())) layer = -1;
    if (std::abs(mesh.layerTargetWeight(layer)) < 1e-3f) return false;
    layer_ = layer;
    record_ = UndoRecord{};
    record_.label = std::move(label);
    record_.layer = layer;
    if (touchStamp_.size() != mesh.vertexCount()) {
        touchStamp_.assign(mesh.vertexCount(), 0);
        maskStamp_.assign(mesh.vertexCount(), 0);
        colorStamp_.assign(mesh.vertexCount(), 0);
        stamp_ = 0;
    }
    if (++stamp_ == 0) {
        std::fill(touchStamp_.begin(), touchStamp_.end(), 0);
        std::fill(maskStamp_.begin(), maskStamp_.end(), 0);
        std::fill(colorStamp_.begin(), colorStamp_.end(), 0);
        stamp_ = 1;
    }
    active_ = true;
    return true;
}

void Sculptor::touch(Mesh& mesh, uint32_t v) {
    if (touchStamp_[v] == stamp_) return;
    touchStamp_[v] = stamp_;
    record_.positions.add(v, mesh.layerTarget(layer_)[v]);
}

void Sculptor::recordMask(const Mesh& mesh, uint32_t v) {
    if (!active_ || maskStamp_[v] == stamp_) return;
    maskStamp_[v] = stamp_;
    record_.mask.add(v, mesh.maskAt(v));
}

void Sculptor::recordColor(const Mesh& mesh, uint32_t v) {
    if (!active_ || colorStamp_[v] == stamp_) return;
    colorStamp_[v] = stamp_;
    record_.colors.add(v, mesh.colorAt(v));
}

// Moves a vertex and attributes the offset to the active layer. Masked and
// hidden vertices are attenuated here, once, for every brush.
void Sculptor::displace(Mesh& mesh, uint32_t v, const glm::vec3& d) {
    const float k = vertexInfluence(mesh, v);
    if (k <= 0.0f) return;
    touch(mesh, v);
    const glm::vec3 dk = d * k;
    mesh.positions[v] += dk;
    if (!mesh.layers.empty()) mesh.layerTarget(layer_)[v] += dk / mesh.layerTargetWeight(layer_);
}

UndoRecord Sculptor::endStroke() {
    active_ = false;
    grab_.clear();
    return std::move(record_);
}

void Sculptor::dab(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p, Symmetry sym) {
    if (!active_) return;
    for (const glm::vec3& m : mirrors(sym)) {
        DabParams mp = p;
        mp.center *= m;
        mp.viewDir *= m;
        mp.strokeDelta *= m;
        mp.alphaUp *= m;
        dabSingle(mesh, bvh, type, mp);
    }
}

void Sculptor::dabSingle(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p) {
    // Alphas and the square ClayBuildup tip reach into the corners of their square.
    const bool useAlpha = p.alpha && !p.alpha->empty();
    const float reach = useAlpha ? 1.4143f : type == BrushType::ClayBuildup ? 1.3f : 1.0f;
    brushVertices(mesh, bvh, p.center, p.radius * reach, verts_);
    if (verts_.empty()) return;

    auto& pos = mesh.positions;
    const auto& nrm = mesh.normals;

    if (p.frontFacesOnly) {
        std::erase_if(verts_, [&](uint32_t v) { return glm::dot(nrm[v], p.viewDir) > 0.0f; });
        if (verts_.empty()) return;
    }

    // Weighted area normal and centre of the brush region.
    glm::vec3 areaNormal(0.0f), areaCenter(0.0f);
    float wsum = 0.0f;
    for (uint32_t v : verts_) {
        float w = brushFalloff(glm::distance(pos[v], p.center), p.radius);
        areaNormal += nrm[v] * w;
        areaCenter += pos[v] * w;
        wsum += w;
    }
    if (wsum <= 0.0f) return;
    areaCenter /= wsum;
    float nlen = glm::length(areaNormal);
    areaNormal = nlen > 1e-12f ? areaNormal / nlen : -p.viewDir;

    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    const float sign = p.invert ? -1.0f : 1.0f;
    const float r = p.radius;

    // Brush-local frame on the area plane (alpha orientation, square tips).
    glm::vec3 up = projectOnPlane(p.alphaUp, areaNormal);
    if (glm::length(up) < 1e-6f)
        up = projectOnPlane(std::abs(areaNormal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0), areaNormal);
    up = glm::normalize(up);
    const glm::vec3 right = glm::cross(up, areaNormal);
    const float strokeLen = glm::length(p.strokeDelta);
    const glm::vec3 strokeDir = strokeLen > 1e-12f ? p.strokeDelta / strokeLen : glm::vec3(0.0f);
    if (type == BrushType::Layer) prepareLayer(mesh, r * 0.08f * std::clamp(p.strength, 0.0f, 1.0f));

    scratch_.resize(verts_.size());
    for (size_t i = 0; i < verts_.size(); ++i) {
        const uint32_t v = verts_[i];
        const glm::vec3& x = pos[v];
        const glm::vec3 o = x - p.center;
        const float f = useAlpha ? p.alpha->sample(glm::dot(o, right) / r, glm::dot(o, up) / r)
                                 : brushFalloff(glm::length(o), r);
        glm::vec3 d(0.0f);

        switch (type) {
        case BrushType::Standard:
            d = areaNormal * (sign * s * r * 0.06f * f);
            break;
        case BrushType::Inflate:
            d = nrm[v] * (sign * s * r * 0.05f * f);
            break;
        case BrushType::Smooth: {
            auto ring = mesh.neighbors(v);
            if (ring.empty()) break;
            glm::vec3 avg(0.0f);
            for (uint32_t n : ring) avg += pos[n];
            avg /= static_cast<float>(ring.size());
            d = (avg - x) * std::min(1.0f, s * f);
            break;
        }
        case BrushType::Flatten: {
            float h = glm::dot(x - areaCenter, areaNormal);
            d = -areaNormal * (h * s * f * (p.invert ? -0.5f : 1.0f));
            break;
        }
        case BrushType::Clay: {
            // Raise a plane slightly above the surface and pull everything below up to it.
            glm::vec3 planePoint = areaCenter + areaNormal * (sign * r * 0.05f * s);
            float h = glm::dot(x - planePoint, areaNormal);
            if (sign * h < 0.0f) d = -areaNormal * (h * f);
            break;
        }
        case BrushType::Crease: {
            // Carves a sharp valley by default (raises a ridge when inverted).
            float sharp = f * f;
            d = areaNormal * (-sign * s * r * 0.05f * sharp);
            d += projectOnPlane(p.center - x, areaNormal) * (0.25f * s * f);
            break;
        }
        case BrushType::Pinch:
            d = projectOnPlane(p.center - x, areaNormal) * (sign * 0.2f * s * f);
            break;
        case BrushType::DamStandard: {
            // Narrow, deep V-groove (a ridge when inverted); the sides are always
            // pinched in toward the stroke line, so the crease stays sharp.
            const float core = f * f * f * f;
            d = areaNormal * (-sign * s * r * 0.08f * core);
            glm::vec3 pull = projectOnPlane(p.center - x, areaNormal);
            pull -= strokeDir * glm::dot(pull, strokeDir);
            d += pull * (0.3f * s * f);
            break;
        }
        case BrushType::ClayBuildup: {
            // Square tip (rounded corners) with a flat top: a raised plane that
            // everything below is pulled up to, so strokes build up in slabs.
            float w = f;
            if (!useAlpha) {
                const float cheb = std::max(std::abs(glm::dot(o, right)), std::abs(glm::dot(o, up))) / r;
                const float corner = std::clamp((1.3f - glm::length(o) / r) / 0.3f, 0.0f, 1.0f);
                const float edge = std::clamp((1.0f - cheb) / 0.35f, 0.0f, 1.0f);
                w = edge * edge * (3.0f - 2.0f * edge) * corner;
            }
            glm::vec3 planePoint = areaCenter + areaNormal * (sign * r * 0.08f * s);
            float h = glm::dot(x - planePoint, areaNormal);
            if (sign * h < 0.0f) d = -areaNormal * (h * w);
            break;
        }
        case BrushType::SnakeHook: {
            // Drag the region along with the stroke; the sides pinch in, so pulled shapes taper.
            d = p.strokeDelta * f;
            glm::vec3 side = p.center - x;
            side -= strokeDir * glm::dot(side, strokeDir);
            d += side * (0.1f * s * f * std::min(1.0f, strokeLen / (0.1f * r)));
            break;
        }
        case BrushType::Trim: {
            // Trim Dynamic: a plane just below the area cuts off whatever sticks out
            // above it (fills up to it when inverted). sqrt falloff for a crisp edge.
            glm::vec3 planePoint = areaCenter - areaNormal * (sign * r * 0.02f * s);
            float h = glm::dot(x - planePoint, areaNormal);
            if (sign * h > 0.0f) d = -areaNormal * (h * std::min(1.0f, std::sqrt(f) * (0.5f + s)));
            break;
        }
        case BrushType::Layer: {
            // Each vertex rises to the stroke's height x falloff once; later dabs
            // only add what is missing, so overlaps within a stroke never stack.
            if (layerStamp_[v] != layerEpoch_) {
                layerStamp_[v] = layerEpoch_;
                layerDone_[v] = 0.0f;
                layerDir_[v] = areaNormal;
            }
            const float target = sign * layerHeight_ * std::clamp(p.pressure, 0.0f, 1.0f) * f;
            if (std::abs(target) > std::abs(layerDone_[v])) {
                d = layerDir_[v] * (target - layerDone_[v]);
                layerDone_[v] = target;
            }
            break;
        }
        case BrushType::Nudge:
            // Slide the surface sideways along the stroke without lifting it.
            d = projectOnPlane(p.strokeDelta, nrm[v]) * (f * std::min(1.0f, 0.4f + s));
            break;
        default:
            break;
        }
        scratch_[i] = d;
    }

    for (size_t i = 0; i < verts_.size(); ++i) displace(mesh, verts_[i], scratch_[i]);
    mesh.updateNormals(verts_);
    bvh.refit(mesh, verts_);
}

void Sculptor::dabRecipe(Mesh& mesh, BVH& bvh, const BrushRecipe& recipe, const DabParams& p, Symmetry sym) {
    if (!active_) return;
    DabParams base = p;
    base.pressure = std::pow(std::clamp(p.pressure, 0.0f, 1.0f), recipe.pressureCurve);
    base.frontFacesOnly = p.frontFacesOnly || recipe.frontFacesOnly;
    for (const glm::vec3& m : mirrors(sym)) {
        DabParams mp = base;
        mp.center *= m;
        mp.viewDir *= m;
        mp.strokeDelta *= m;
        mp.alphaUp *= m;
        dabRecipeSingle(mesh, bvh, recipe, mp);
    }
}

void Sculptor::dabRecipeSingle(Mesh& mesh, BVH& bvh, const BrushRecipe& r, const DabParams& p) {
    brushVertices(mesh, bvh, p.center, p.radius, verts_);
    if (verts_.empty()) return;
    const auto& pos = mesh.positions;
    const auto& nrm = mesh.normals;
    // An alpha (if any) modulates the recipe's tip, oriented on the view-facing plane.
    const bool useAlpha = p.alpha && !p.alpha->empty();
    glm::vec3 aUp = projectOnPlane(p.alphaUp, p.viewDir);
    aUp = glm::length(aUp) > 1e-6f ? glm::normalize(aUp) : glm::vec3(0, 1, 0);
    const glm::vec3 aRight = glm::cross(aUp, -p.viewDir);
    auto alphaWeight = [&](const glm::vec3& x) {
        const glm::vec3 o = x - p.center;
        return p.alpha->sample(glm::dot(o, aRight) / p.radius, glm::dot(o, aUp) / p.radius);
    };
    if (p.frontFacesOnly) {
        std::erase_if(verts_, [&](uint32_t v) { return glm::dot(nrm[v], p.viewDir) > 0.0f; });
        if (verts_.empty()) return;
    }

    const float rad = p.radius;
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * p.pressure;
    const float sign = p.invert ? -1.0f : 1.0f;

    // Per-vertex weight: tip shape, cavity mask and pattern.
    weights_.resize(verts_.size());
    const float edge = std::max(0.05f, 1.0f - r.hardness * 0.95f);
    const float exponent = 1.0f + r.tipSharpness * 5.0f;
    const float patternFreq = 1.0f / std::max(rad * r.patternScale, 1e-6f);
    glm::vec3 areaNormal(0.0f), areaCenter(0.0f);
    float wsum = 0.0f;
    for (size_t i = 0; i < verts_.size(); ++i) {
        const uint32_t v = verts_[i];
        float t = std::clamp((1.0f - glm::distance(pos[v], p.center) / rad) / edge, 0.0f, 1.0f);
        float w = std::pow(t * t * (3.0f - 2.0f * t), exponent);
        areaNormal += nrm[v] * w;
        areaCenter += pos[v] * w;
        wsum += w;
        if (r.cavityMask != 0.0f) {
            auto ring = mesh.neighbors(v);
            if (!ring.empty()) {
                glm::vec3 avg(0.0f);
                float edgeLen = 0.0f;
                for (uint32_t n : ring) {
                    avg += pos[n];
                    edgeLen += glm::distance(pos[n], pos[v]);
                }
                avg /= float(ring.size());
                edgeLen /= float(ring.size());
                // > 0 in cavities (neighbours sit above the vertex), < 0 on ridges.
                float c = std::clamp(glm::dot(nrm[v], avg - pos[v]) / std::max(edgeLen, 1e-9f) * 6.0f, -1.0f, 1.0f);
                float m = r.cavityMask > 0.0f ? std::max(c, 0.0f) : std::max(-c, 0.0f);
                w *= glm::mix(1.0f, m, std::abs(r.cavityMask));
            }
        }
        if (r.pattern != BrushRecipe::Pattern::None && r.patternAmount > 0.0f)
            w *= glm::mix(1.0f, samplePattern(r.pattern, pos[v] * patternFreq), r.patternAmount);
        if (useAlpha) w *= alphaWeight(pos[v]);
        weights_[i] = w;
    }
    if (wsum <= 0.0f) return;
    areaCenter /= wsum;
    float nlen = glm::length(areaNormal);
    areaNormal = nlen > 1e-12f ? areaNormal / nlen : -p.viewDir;

    glm::vec3 strokeDir = p.strokeDelta;
    float strokeLen = glm::length(strokeDir);
    strokeDir = strokeLen > 1e-12f ? strokeDir / strokeLen : glm::vec3(0.0f);

    scratch_.resize(verts_.size());
    for (size_t i = 0; i < verts_.size(); ++i) {
        const uint32_t v = verts_[i];
        const glm::vec3& x = pos[v];
        const float f = weights_[i];
        glm::vec3 dir;
        switch (r.direction) {
        case BrushRecipe::Direction::Inflate: dir = nrm[v]; break;
        case BrushRecipe::Direction::View: dir = -p.viewDir; break;
        case BrushRecipe::Direction::Stroke: dir = strokeDir; break;
        default: dir = areaNormal; break;
        }
        glm::vec3 d = dir * (sign * r.depth * 0.12f * rad * s * f);

        const float h = glm::dot(x - areaCenter, areaNormal);
        switch (r.plane) {
        case BrushRecipe::Plane::Clay: {
            glm::vec3 planePoint = areaCenter + areaNormal * (sign * rad * 0.06f * s);
            float hc = glm::dot(x - planePoint, areaNormal);
            if (sign * hc < 0.0f) d -= areaNormal * (hc * f * r.planeAmount);
            break;
        }
        case BrushRecipe::Plane::Flatten: d -= areaNormal * (h * s * f * r.planeAmount); break;
        case BrushRecipe::Plane::Fill:
            if (h < 0.0f) d -= areaNormal * (h * s * f * r.planeAmount);
            break;
        case BrushRecipe::Plane::Scrape:
            if (h > 0.0f) d -= areaNormal * (h * s * f * r.planeAmount);
            break;
        default: break;
        }
        if (r.pinch != 0.0f) d += projectOnPlane(p.center - x, areaNormal) * (r.pinch * 0.25f * s * f);
        if (r.drag > 0.0f) d += p.strokeDelta * (r.drag * f);
        scratch_[i] = d;
    }
    for (size_t i = 0; i < verts_.size(); ++i) displace(mesh, verts_[i], scratch_[i]);

    if (r.smooth > 0.0f) {
        for (size_t i = 0; i < verts_.size(); ++i) {
            auto ring = mesh.neighbors(verts_[i]);
            if (ring.empty()) {
                scratch_[i] = glm::vec3(0.0f);
                continue;
            }
            glm::vec3 avg(0.0f);
            for (uint32_t n : ring) avg += pos[n];
            avg /= float(ring.size());
            scratch_[i] = (avg - pos[verts_[i]]) * std::min(1.0f, r.smooth * weights_[i]);
        }
        for (size_t i = 0; i < verts_.size(); ++i) displace(mesh, verts_[i], scratch_[i]);
    }

    mesh.updateNormals(verts_);
    bvh.refit(mesh, verts_);
}

void Sculptor::beginGrab(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym) {
    grab_.clear();
    grabVerts_.clear();
    if (!active_) return;
    for (const glm::vec3& m : mirrors(sym)) {
        glm::vec3 c = p.center * m;
        brushVertices(mesh, bvh, c, p.radius, verts_);
        for (uint32_t v : verts_) {
            float w = brushFalloff(glm::distance(mesh.positions[v], c), p.radius) *
                      std::clamp(p.strength * 2.0f, 0.0f, 1.0f);
            if (w <= 0.0f) continue;
            grab_.push_back({v, w, m});
            grabVerts_.push_back(v);
        }
    }
    std::sort(grabVerts_.begin(), grabVerts_.end());
    grabVerts_.erase(std::unique(grabVerts_.begin(), grabVerts_.end()), grabVerts_.end());
}

void Sculptor::grab(Mesh& mesh, BVH& bvh, const glm::vec3& delta) {
    if (grab_.empty()) return;
    for (const auto& g : grab_) displace(mesh, g.vertex, delta * g.mirror * g.weight);
    mesh.updateNormals(grabVerts_);
    bvh.refit(mesh, grabVerts_);
}

// Puts every vertex the current stroke moved back to its stroke-start position.
void Sculptor::restoreStroke(Mesh& mesh, BVH& bvh) {
    if (!active_) return;
    // Polypaint laid down by the stroke so far (Paint brush / RGB while sculpting).
    if (!record_.colors.empty() && mesh.hasColors()) {
        const auto& ci = record_.colors.indices;
        for (size_t i = 0; i < ci.size(); ++i) {
            mesh.colors[ci[i]] = record_.colors.values[i];
            mesh.markDirty(ci[i]); // paint-only strokes return before updateNormals marks anything
        }
    }
    if (record_.positions.empty()) return;
    auto& target = mesh.layerTarget(layer_);
    const auto& idx = record_.positions.indices;
    for (size_t i = 0; i < idx.size(); ++i) target[idx[i]] = record_.positions.values[i];
    mesh.evaluate(idx);
    mesh.updateNormals(idx);
    bvh.refit(mesh, idx);
    // The Layer brush starts over too.
    if (layerEpoch_ != 0 && ++layerEpoch_ == 0) {
        std::fill(layerStamp_.begin(), layerStamp_.end(), 0);
        layerEpoch_ = 1;
    }
}

// Sizes the Layer brush state and starts a fresh height map for each new stroke.
void Sculptor::prepareLayer(const Mesh& mesh, float height) {
    if (layerStamp_.size() != mesh.vertexCount()) {
        layerStamp_.assign(mesh.vertexCount(), 0);
        layerDone_.assign(mesh.vertexCount(), 0.0f);
        layerDir_.assign(mesh.vertexCount(), glm::vec3(0.0f));
        layerEpoch_ = 0;
    }
    if (layerEpoch_ != 0 && layerSeenStamp_ == stamp_) return;
    layerSeenStamp_ = stamp_;
    layerHeight_ = height;
    if (++layerEpoch_ == 0) {
        std::fill(layerStamp_.begin(), layerStamp_.end(), 0);
        layerEpoch_ = 1;
    }
}

} // namespace sf
