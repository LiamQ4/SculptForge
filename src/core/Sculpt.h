// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/BrushRecipe.h"
#include "core/Mesh.h"
#include "core/Undo.h"

#include <chrono>
#include <string>
#include <vector>

namespace sf {

struct BrushAlpha;

enum class BrushType {
    Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch,
    DamStandard, // sharp V-crease with the sides pinched toward the stroke
    ClayBuildup, // square-ish, flat-topped clay
    SnakeHook,   // pulls the region along the stroke (strokeDelta), tapering
    Trim,        // Trim Dynamic: a plane cuts away everything above it
    Layer,       // raises to one fixed height per stroke (overlaps never stack)
    Nudge,       // slides the surface sideways along the stroke
    Count
};

const char* brushName(BrushType type);

struct DabParams {
    glm::vec3 center{0.0f};             // surface point under the cursor
    glm::vec3 viewDir{0.0f, 0.0f, -1.0f}; // camera ray direction (into the scene)
    float radius = 0.1f;                // world units
    float strength = 0.5f;              // 0..1
    float pressure = 1.0f;              // tablet pressure, 0..1
    bool invert = false;
    bool frontFacesOnly = false;
    glm::vec3 strokeDelta{0.0f}; // world-space movement since the previous dab (recipe, SnakeHook, Nudge, DamStandard)
    const BrushAlpha* alpha = nullptr;   // stamp shaping the falloff (null/empty = round)
    glm::vec3 alphaUp{0.0f, 1.0f, 0.0f}; // world direction of the alpha's top edge (screen up, or rotated)
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

// Where brush time goes (core/Benchmark.h, Help > Performance): milliseconds summed over dabs since the
// counters were last zeroed. kernel = finding the vertices and moving / masking / painting them.
struct SculptTimings {
    double kernelMs = 0.0, normalsMs = 0.0, bvhMs = 0.0;
    uint64_t dabs = 0, vertices = 0;
};
// Milliseconds since `t`, which moves on to now (for SculptTimings).
inline double lapMs(std::chrono::steady_clock::time_point& t) {
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - t).count();
    t = now;
    return ms;
}

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

    SculptTimings timings;

    // Mirror sign vectors for the enabled symmetry axes (always starts with (1,1,1)).
    static std::vector<glm::vec3> mirrors(Symmetry sym);

    // Puts every vertex the current stroke moved (or painted) back the way it
    // started (the stroke stays open). DragRect re-stamps from scratch while dragging.
    void restoreStroke(Mesh& mesh, BVH& bvh);

private:
    struct GrabEntry {
        uint32_t vertex;
        float weight;
        glm::vec3 mirror;
    };

    void dabSingle(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p);
    void dabRecipeSingle(Mesh& mesh, BVH& bvh, const BrushRecipe& r, const DabParams& p);
    void finishDab(Mesh& mesh, BVH& bvh, std::span<const uint32_t> verts, std::chrono::steady_clock::time_point clock);
    void touch(Mesh& mesh, uint32_t v);
    // The one place brushes move vertices: scales `d` by vertexInfluence().
    void displace(Mesh& mesh, uint32_t v, const glm::vec3& d);
    void displaceAll(Mesh& mesh, std::span<const uint32_t> verts, std::span<const glm::vec3> d);

    bool active_ = false;
    int layer_ = -1;
    UndoRecord record_;
    std::vector<uint32_t> touchStamp_, maskStamp_, colorStamp_;
    uint32_t stamp_ = 0;

    std::vector<uint32_t> verts_;
    std::vector<glm::vec3> scratch_;
    std::vector<float> weights_, influence_;
    std::vector<GrabEntry> grab_;
    std::vector<uint32_t> grabVerts_;

    // Layer brush: per-stroke height already applied to each vertex.
    void prepareLayer(const Mesh& mesh, float height);
    std::vector<uint32_t> layerStamp_;
    std::vector<float> layerDone_;
    std::vector<glm::vec3> layerDir_;
    uint32_t layerEpoch_ = 0, layerSeenStamp_ = 0;
    float layerHeight_ = 0.0f;

public:
    // Mask brush (Ctrl in the app; defined in Masking.cpp): raises the mask
    // under the brush towards 1, or lowers it towards 0 when p.invert is set.
    // Ignores the existing mask (it edits it) but skips hidden vertices.
    void dabMask(Mesh& mesh, const BVH& bvh, const DabParams& p, Symmetry sym);
};

} // namespace sf
