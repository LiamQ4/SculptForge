// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace sf {

struct AABB {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    void expand(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void expand(const AABB& b) {
        min = glm::min(min, b.min);
        max = glm::max(max, b.max);
    }
    bool valid() const { return min.x <= max.x; }
    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extent() const { return max - min; }
};

// A sculpt layer: per-vertex offsets blended in with a weight. Layers are
// exported as glTF morph targets (blend shapes) for facial/corrective animation.
struct SculptLayer {
    std::string name;
    std::vector<glm::vec3> delta;
    float weight = 1.0f;
};

// Triangle mesh with vertex adjacency, suitable for sculpting.
//
// Optional attributes (see docs/architecture-attributes.md) are empty until
// a feature needs them, so plain meshes pay nothing. When present their size
// always matches the vertex / triangle / corner count; rebuildTopology()
// drops any that don't.
class Mesh {
public:
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::uvec3> triangles;

    // Per vertex: mask (0 = free, 1 = fully masked) and polypaint (linear RGB, 0..1).
    std::vector<float> mask;
    std::vector<glm::vec3> colors;
    // Per triangle: polygroup id (empty = everything in group 0) and hidden flag (empty = all visible).
    std::vector<uint32_t> polygroups;
    std::vector<uint8_t> hidden;
    // Per triangle corner (3 per triangle, in triangle order): texture coordinates.
    std::vector<glm::vec2> uvs;
    // Per triangle: the triangle it forms a quad with (kNoQuad = a plain triangle; empty = no quads). Imported
    // quads are split into two triangles that keep each other here, so the wireframe and the exporters can
    // show and write them as quads again. Pairs are symmetric and share one edge, wound oppositely;
    // sanitizeAttributes() unpairs anything else, so topology changes can never leave a stale pair.
    std::vector<uint32_t> quadPartners;
    static constexpr uint32_t kNoQuad = ~0u;

    bool hasMask() const { return !mask.empty(); }
    bool hasColors() const { return !colors.empty(); }
    bool hasPolygroups() const { return !polygroups.empty(); }
    bool hasHidden() const { return !hidden.empty(); }
    bool hasUvs() const { return !uvs.empty(); }
    bool hasQuads() const { return !quadPartners.empty(); }

    float maskAt(uint32_t v) const { return mask.empty() ? 0.0f : mask[v]; }
    glm::vec3 colorAt(uint32_t v, const glm::vec3& fallback = glm::vec3(1.0f)) const {
        return colors.empty() ? fallback : colors[v];
    }
    uint32_t polygroupAt(uint32_t t) const { return polygroups.empty() ? 0u : polygroups[t]; }
    bool triangleHidden(uint32_t t) const { return !hidden.empty() && hidden[t] != 0; }
    // The triangle `t` is shown and exported as one quad with, or kNoQuad: a stored pair whose halves share a
    // polygroup (a pair split between groups shows and exports as two triangles).
    uint32_t quadPartnerAt(uint32_t t) const {
        if (quadPartners.empty()) return kNoQuad;
        const uint32_t u = quadPartners[t];
        return u < quadPartners.size() && polygroupAt(t) == polygroupAt(u) ? u : kNoQuad;
    }
    // Which edge of `t` (t[e] -> t[(e + 1) % 3]) is the diagonal it shares with triangle `u` (wound the other
    // way in `u`, whose third corner is not on `t`), or -1 when they don't form a quad.
    int quadDiagonal(uint32_t t, uint32_t u) const;
    // Face for exporting `t`: returns 4 with the quad's corners in winding order (t[e+1], t[e+2], t[e], then the
    // partner's far corner, so an (a,b,c) + (a,c,d) split gives (a,b,c,d) back), 3 with the triangle, or 0 when
    // `t` is the second half of a quad written with its partner.
    int exportFace(uint32_t t, glm::uvec4& face) const;
    // Number of quads (pairs counted once by quadPartnerAt).
    size_t quadCount() const;
    // True when every triangle around `v` is hidden (such vertices must not be edited).
    bool vertexHidden(uint32_t v) const;

    // Allocate an attribute (no-op if present); returns it for convenience.
    std::vector<float>& ensureMask(float fill = 0.0f);
    std::vector<glm::vec3>& ensureColors(const glm::vec3& fill = glm::vec3(1.0f));
    std::vector<uint32_t>& ensurePolygroups(uint32_t fill = 0);
    std::vector<uint8_t>& ensureHidden();
    std::vector<uint32_t>& ensureQuadPartners();
    // Records triangles `t` and `u` as one quad (growing quadPartners to the triangle count, so it can be called
    // while triangles are being added).
    void pairQuad(uint32_t t, uint32_t u);
    // Free an attribute again once it holds only its default (e.g. after "clear mask" / "show all").
    void clearMask() { std::vector<float>().swap(mask); }
    void clearColors() { std::vector<glm::vec3>().swap(colors); }
    void clearPolygroups() { std::vector<uint32_t>().swap(polygroups); }
    void clearHidden() { std::vector<uint8_t>().swap(hidden); }
    void clearQuads() { std::vector<uint32_t>().swap(quadPartners); }
    // Drops attributes whose size no longer matches the mesh and unpairs invalid quad pairs (called by
    // rebuildTopology).
    void sanitizeAttributes();

    // Sculpt layers. While any exist, `base` holds the positions with every
    // layer at zero and positions == base + sum(layer.weight * layer.delta).
    std::vector<glm::vec3> base;
    std::vector<SculptLayer> layers;

    int addLayer(std::string name);
    void removeLayer(int index);
    // Re-evaluates every vertex and recomputes normals (BVH must be rebuilt by the caller).
    void setLayerWeight(int index, float weight);
    // The array a stroke on `layer` writes to (-1 = base), and the weight its offsets are scaled by.
    std::vector<glm::vec3>& layerTarget(int layer);
    float layerTargetWeight(int layer) const;
    // Recomputes positions of `verts` from base + layers.
    void evaluate(std::span<const uint32_t> verts);
    void evaluateAll();
    const std::vector<glm::vec3>& basePositions() const { return layers.empty() ? positions : base; }

    size_t vertexCount() const { return positions.size(); }
    size_t triangleCount() const { return triangles.size(); }

    // Must be called after the triangle list or vertex count changes.
    void rebuildTopology();
    void recomputeAllNormals();
    // Recompute normals of the faces around `verts` and of every vertex they touch.
    void updateNormals(std::span<const uint32_t> verts);

    std::span<const uint32_t> neighbors(uint32_t v) const {
        return {adjList_.data() + adjOffsets_[v], adjList_.data() + adjOffsets_[v + 1]};
    }
    std::span<const uint32_t> vertexTriangles(uint32_t v) const {
        return {vtList_.data() + vtOffsets_[v], vtList_.data() + vtOffsets_[v + 1]};
    }

    AABB bounds() const;

    // GPU synchronisation state: [dirtyBegin, dirtyEnd) needs re-uploading,
    // topologyDirty means everything (including indices) does.
    // Vertex attributes (mask, colours) share the vertex range; facesDirty
    // means per-triangle state (hidden, polygroups) needs re-uploading.
    uint32_t dirtyBegin = std::numeric_limits<uint32_t>::max();
    uint32_t dirtyEnd = 0;
    bool topologyDirty = true;
    bool facesDirty = true;

    void markDirty(uint32_t v) {
        dirtyBegin = std::min(dirtyBegin, v);
        dirtyEnd = std::max(dirtyEnd, v + 1);
    }
    void clearDirty() {
        dirtyBegin = std::numeric_limits<uint32_t>::max();
        dirtyEnd = 0;
        topologyDirty = false;
        facesDirty = false;
    }
    void markAllDirty() {
        if (positions.empty()) return;
        dirtyBegin = 0;
        dirtyEnd = static_cast<uint32_t>(positions.size());
    }

private:
    uint32_t nextStamp();

    std::vector<uint32_t> adjOffsets_, adjList_;
    std::vector<uint32_t> vtOffsets_, vtList_;
    std::vector<glm::vec3> faceNormals_;

    std::vector<uint32_t> triStamp_, vertStamp_;
    uint32_t stamp_ = 0;
    std::vector<uint32_t> scratchTris_, scratchVerts_;
};

// Shape-preserving Taubin (lambda/mu) smoothing of positions; needs topology.
// Ignores sculpt layers, so use it on freshly built meshes.
void taubinSmooth(Mesh& m, int iterations);

// Merges coincident vertices (UV/normal seams from importers), drops
// degenerate triangles and builds topology and normals. `layers` (optional)
// hold per-vertex deltas indexed like m.positions and become sculpt layers.
// Optional attributes of `m` are carried over (vertex ones from the first
// vertex of each welded group, triangle/corner ones per kept triangle).
Mesh weldImported(const Mesh& m, const std::vector<SculptLayer>& layers = {});

// Carries the quad pairs of `src` over to `dst`, built from some of its triangles: `newIndex[t]` is the index of
// src triangle t in dst (Mesh::kNoQuad = dropped). Pairs that lost a half are dropped; call before
// dst.rebuildTopology(), which checks the rest.
void remapQuads(const Mesh& src, const std::vector<uint32_t>& newIndex, Mesh& dst);
// Appends the quad pairs of `src` (whose triangles were appended to `dst` after `firstTriangle` others).
void appendQuads(Mesh& dst, size_t firstTriangle, const Mesh& src);

// Mesh::colors are linear RGB; files that store display colours (OBJ, PLY) convert.
glm::vec3 srgbToLinear(const glm::vec3& c);
glm::vec3 linearToSrgb(const glm::vec3& c);

// Gives every vertex of `dst` the mask and colour of the nearest vertex of
// `src` (only the attributes `src` has). For rebuilt surfaces such as voxel
// remesh or re-imported scans; needs no topology.
void transferVertexAttributes(const Mesh& src, Mesh& dst);

} // namespace sf
