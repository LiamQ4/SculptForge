// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/BrushRecipe.h"
#include "core/Mesh.h"
#include "core/Sculpt.h"
#include "core/Splats.h"
#include "core/Undo.h"

#include <string>

namespace sf {

// Everything that makes up the open scene: the mesh, its acceleration
// structure and edit history. UI-independent so it can be driven by tests,
// scripts or an AI assistant through the command registry.
struct Document {
    Mesh mesh;
    BVH bvh;
    UndoStack undo;
    Sculptor sculptor;
    std::string path; // empty until saved/loaded
    bool modified = false;
    int activeLayer = -1; // sculpt layer strokes write into (-1 = base)
    // Recipe brush library (built-ins, loaded .sfbrush files, AI-made brushes).
    std::vector<BrushRecipe> brushes = builtinBrushRecipes();
    int activeBrush = -1; // index into `brushes`, -1 = use a built-in brush type
    // Adds or replaces (by name) a recipe and returns its index.
    int addBrush(BrushRecipe recipe);
    // Gaussian splats shown alongside the mesh (a capture used as reference or
    // source, or the sculpt converted for splat viewers). Not part of undo.
    SplatCloud splats;
    uint64_t splatsVersion = 0; // bump after editing `splats` so views re-upload
    void splatsChanged() { ++splatsVersion; }
    bool bvhDirty = false; // set after whole-mesh deformations (e.g. layer weights)

    void syncBvh() {
        if (bvhDirty) bvh.build(mesh);
        bvhDirty = false;
    }
    // Asset name derived from the document path (for exports).
    std::string assetName() const;

    // Replaces the mesh and clears the edit history.
    void setMesh(Mesh m);
    // Replaces the mesh as an undoable topology change.
    void replaceMeshUndoable(Mesh m, std::string label);
};

} // namespace sf
