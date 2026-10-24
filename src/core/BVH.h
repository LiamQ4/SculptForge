// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/IndexBits.h"
#include "core/IndexBits.h"
#include "core/Mesh.h"

#include <span>
#include <vector>

namespace sf {

struct RayHit {
    float t = 0.0f;
    uint32_t triangle = 0;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f}; // geometric normal of the hit triangle
};

// Bounding volume hierarchy over a mesh's triangles. Supports ray casts,
// sphere queries for brushes, and cheap incremental refits while sculpting.
class BVH {
public:
    void build(const Mesh& mesh);
    // Nearest hit along the ray; hidden triangles (Mesh::hidden) are skipped.
    bool raycast(const Mesh& mesh, const glm::vec3& origin, const glm::vec3& dir, RayHit& hit) const;
    // Appends every vertex within `radius` of `center` to `out` (cleared first), in increasing index order.
    // Ignores visibility; brushes use sf::brushVertices, which filters hidden ones.
    void queryVertices(const Mesh& mesh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) const;
    // Appends every triangle whose bounds touch the sphere (cleared first; hidden ones included).
    void queryTriangles(const Mesh& mesh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) const;
    // Updates bounds after the given vertices moved. Bounds only ever grow
    // conservatively for ancestors, which keeps queries correct.
    void refit(const Mesh& mesh, std::span<const uint32_t> movedVerts);
    // refit() after only vertices the last queryVertices() returned moved (brush dabs): the query already
    // found the leaves holding them, so this skips looking them up.
    void refitQueried(const Mesh& mesh);
    // Recomputes every box (same tree) after most of the mesh moved, e.g. a whole SubTool transformed.
    void refitAll(const Mesh& mesh);

    size_t nodeCount() const { return nodes_.size(); }
    size_t memoryBytes() const;

private:
    struct Node {
        AABB box;
        uint32_t first = 0; // leaf: offset into triIndices_, inner: index of left child (right = first + 1)
        uint32_t count = 0; // > 0 for leaves
        uint32_t parent = 0;
    };

    AABB leafBox(const Mesh& mesh, const Node& n) const;
    void recomputeLeaf(const Mesh& mesh, Node& n); // its box, and triLeaf_ for its triangles
    void refitLeaves(const Mesh& mesh, std::span<const uint32_t> leaves);

    std::vector<Node> nodes_;
    std::vector<uint32_t> triIndices_;
    std::vector<uint32_t> triLeaf_;

    mutable IndexBits vertBits_;               // queryVertices: the vertices found so far
    mutable std::vector<uint32_t> queryLeaves_; // the leaves holding them, for refitQueried()
    mutable std::vector<uint32_t> queryTasks_;
    mutable std::vector<std::vector<uint32_t>> queryParts_;
    IndexBits leafBits_; // refit: the leaves to recompute
    std::vector<uint32_t> scratchLeaves_;
    mutable std::vector<uint32_t> stack_;
};

} // namespace sf
