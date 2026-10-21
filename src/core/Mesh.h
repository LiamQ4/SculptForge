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
class Mesh {
public:
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::uvec3> triangles;

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
    uint32_t dirtyBegin = std::numeric_limits<uint32_t>::max();
    uint32_t dirtyEnd = 0;
    bool topologyDirty = true;

    void markDirty(uint32_t v) {
        dirtyBegin = std::min(dirtyBegin, v);
        dirtyEnd = std::max(dirtyEnd, v + 1);
    }
    void clearDirty() {
        dirtyBegin = std::numeric_limits<uint32_t>::max();
        dirtyEnd = 0;
        topologyDirty = false;
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
Mesh weldImported(const Mesh& m, const std::vector<SculptLayer>& layers = {});

} // namespace sf
