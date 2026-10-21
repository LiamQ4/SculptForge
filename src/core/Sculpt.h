// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/BrushRecipe.h"
#include "core/Mesh.h"
#include "core/Undo.h"

#include <string>
#include <vector>

namespace sf {

enum class BrushType { Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch, Count };

const char* brushName(BrushType type);

struct DabParams {
    glm::vec3 center{0.0f};             // surface point under the cursor
    glm::vec3 viewDir{0.0f, 0.0f, -1.0f}; // camera ray direction (into the scene)
    float radius = 0.1f;                // world units
    float strength = 0.5f;              // 0..1
    float pressure = 1.0f;              // tablet pressure, 0..1
    bool invert = false;
    bool frontFacesOnly = false;
    glm::vec3 strokeDelta{0.0f}; // world-space movement since the previous dab (recipe brushes)
};

struct Symmetry {
    bool x = true, y = false, z = false;
};

// --- Shared brush helpers (every brush, including future paint/mask brushes, goes through these) ---

// Strength multiplier for vertex `v`: 0 if every triangle around it is
// hidden, otherwise 1 - mask. Applied centrally by Sculptor::displace, so
// all displacement brushes respect mask and visibility. Paint brushes should
// scale by it too; brushes that edit the mask itself must not.
inline float vertexInfluence(const Mesh& mesh, uint32_t v) {
    if (mesh.hasHidden() && mesh.vertexHidden(v)) return 0.0f;
    return 1.0f - mesh.maskAt(v);
}
// Vertices within `radius` of `center` that a brush may touch (fully hidden ones excluded).
void brushVertices(const Mesh& mesh, const BVH& bvh, const glm::vec3& center, float radius, std::vector<uint32_t>& out);
// Smoothstep brush falloff: 1 at the centre, 0 at `radius`.
float brushFalloff(float dist, float radius);

// Applies brush dabs to a mesh, keeping normals and the BVH in sync and
// recording the original positions of every touched vertex for undo.
class Sculptor {
public:
    // Strokes write into sculpt layer `layer` (-1 = base). Returns false if the
    // layer's weight is ~0, since its offsets would be invisible.
    bool beginStroke(const Mesh& mesh, std::string label, int layer = -1);
    void dab(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p, Symmetry sym);
    // Custom brush defined by a recipe (see BrushRecipe.h).
    void dabRecipe(Mesh& mesh, BVH& bvh, const BrushRecipe& recipe, const DabParams& p, Symmetry sym);

    // Move brush: capture the region once, then drag it by world deltas.
    void beginGrab(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym);
    void grab(Mesh& mesh, BVH& bvh, const glm::vec3& delta);

    // Brushes that change mask or polypaint call these before writing
    // mesh.mask[v] / mesh.colors[v] (after ensureMask()/ensureColors()), so
    // the stroke's undo step restores them. Each vertex is recorded once per stroke.
    void recordMask(const Mesh& mesh, uint32_t v);
    void recordColor(const Mesh& mesh, uint32_t v);

    UndoRecord endStroke();
    bool strokeActive() const { return active_; }

    // Mirror sign vectors for the enabled symmetry axes (always starts with (1,1,1)).
    static std::vector<glm::vec3> mirrors(Symmetry sym);

private:
    struct GrabEntry {
        uint32_t vertex;
        float weight;
        glm::vec3 mirror;
    };

    void dabSingle(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p);
    void dabRecipeSingle(Mesh& mesh, BVH& bvh, const BrushRecipe& r, const DabParams& p);
    void touch(Mesh& mesh, uint32_t v);
    // The one place brushes move vertices: scales `d` by vertexInfluence().
    void displace(Mesh& mesh, uint32_t v, const glm::vec3& d);

    bool active_ = false;
    int layer_ = -1;
    UndoRecord record_;
    std::vector<uint32_t> touchStamp_, maskStamp_, colorStamp_;
    uint32_t stamp_ = 0;

    std::vector<uint32_t> verts_;
    std::vector<glm::vec3> scratch_;
    std::vector<float> weights_;
    std::vector<GrabEntry> grab_;
    std::vector<uint32_t> grabVerts_;
};

} // namespace sf
