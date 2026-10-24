// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The CPU side of the renderer's mesh drawing, kept apart from GL so tests can check it:
//
// - The compact vertex the GPU keeps (12 bytes, was 28): the position on a 21-bit grid over the mesh's bounds
//   (with a margin, so sculpting rarely leaves it; one grid for the whole mesh, so a vertex copied into two
//   clusters lands on exactly the same point and seams can't crack), an octahedral normal (2 x 11 bits) and the
//   curvature the shading uses for cavities (10 bits). The shader decodes it the same way (Renderer.cpp).
// - Culling per cluster (core/UploadOrder.h): outside the view, or every triangle facing away. The second only
//   holds for a closed mesh seen from outside (closedOriented, and nothing cut by the near plane): its back
//   faces are then always behind front faces. Open meshes, hidden triangles and views from inside draw them.
#pragma once

#include "core/Mesh.h"
#include "core/UploadOrder.h"

#include <algorithm>
#include <cstdint>

namespace sf {

// Positions: origin + q * step, q in [0, 2^21) per axis.
struct PositionGrid {
    glm::vec3 origin{0.0f};
    float step = 1.0f;
    static constexpr uint32_t kMax = (1u << 21) - 1;
    // A grid over `box` with a margin of an eighth of its size on every side.
    static PositionGrid around(const AABB& box);
    bool contains(const glm::vec3& p) const;
};

struct PackedVertex {
    uint32_t pos[2]; // x: bits 0-20, y: 21-41, z: 42-62
    uint32_t normal; // octahedral x, y (11 bits each, signed), curvature (10 bits, signed)
};
static_assert(sizeof(PackedVertex) == 12);

// Packs a vertex; returns false (and clamps) when p is outside the grid.
bool packVertex(const PositionGrid& grid, const glm::vec3& p, const glm::vec3& n, float curvature, PackedVertex& out);
// What the shader decodes (tests).
glm::vec3 unpackPosition(const PositionGrid& grid, const PackedVertex& v);
glm::vec3 unpackNormal(const PackedVertex& v);
float unpackCurvature(const PackedVertex& v);
constexpr float kCurvatureRange = 0.25f; // stored range +-; the shading saturates at 0.16 / 0.2

// Polypaint colours in 8 bits per channel, gamma 2.2 encoded so dark colours keep their steps (the shader
// decodes with pow 2.2). A table lookup, it is called for every vertex of an upload.
uint32_t packColor(const glm::vec3& linear);
inline uint8_t packMask(float m) { return uint8_t(std::clamp(m, 0.0f, 1.0f) * 255.0f + 0.5f); }

// A cluster's bounding sphere and the cone holding its triangles' face normals (axis, half-angle in radians;
// coneAngle < 0: no useful cone).
struct ClusterBounds {
    glm::vec3 center{0.0f};
    float radius = -1.0f; // < 0: empty
    glm::vec3 axis{0.0f, 0.0f, 1.0f};
    float coneAngle = -1.0f;
};
ClusterBounds clusterBounds(const Mesh& mesh, const UploadOrder& order, uint32_t cluster);

// Every directed edge a->b has as many b->a: the mesh is the closed, consistently wound boundary of a volume
// (it may intersect itself). Then from outside, every back face lies behind a front face.
bool closedOriented(const Mesh& mesh);

// One frame's culling: the view's frustum planes, the eye, and whether back-facing clusters may go.
struct CullView {
    glm::vec4 planes[6];
    glm::vec3 eye{0.0f};
    float nearDistance = 0.0f;
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    bool backfaces = false; // cull clusters facing away
    static CullView from(const glm::mat4& view, const glm::mat4& proj);
};
bool insideFrustum(const CullView& v, const ClusterBounds& b);
bool facingAway(const CullView& v, const ClusterBounds& b); // every triangle of the cluster faces away from the eye

} // namespace sf
