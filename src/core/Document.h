// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/BrushAlpha.h"
#include "core/BrushRecipe.h"
#include "core/Json.h"
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
    bool modified = false; // set by edits the undo stack doesn't see (e.g. layer weights)
    int activeLayer = -1; // sculpt layer strokes write into (-1 = base)
    // Recipe brush library (built-ins, loaded .sfbrush files, AI-made brushes).
    std::vector<BrushRecipe> brushes = builtinBrushRecipes();
    int activeBrush = -1; // index into `brushes`, -1 = use a built-in brush type
    BrushAlpha brushAlpha; // stamp shaping every brush's falloff (empty = round); brush_set_alpha
    glm::vec3 paintColor{0.6f, 0.03f, 0.02f}; // current polypaint colour (linear RGB; see set_color)
    // Adds or replaces (by name) a recipe and returns its index.
    int addBrush(BrushRecipe recipe);
    // Gaussian splats shown alongside the mesh (a capture used as reference or
    // source, or the sculpt converted for splat viewers). Not part of undo.
    SplatCloud splats;
    uint64_t splatsVersion = 0; // bump after editing `splats` so views re-upload
    void splatsChanged() { ++splatsVersion; }
    bool bvhDirty = false; // set after whole-mesh deformations (e.g. layer weights)
    // SubTool name of a mesh that came from ZBrush (GoZ), so sending it back updates that SubTool.
    std::string gozName;

    void syncBvh() {
        if (bvhDirty) bvh.build(mesh);
        bvhDirty = false;
    }
    // Asset name derived from the document path (for exports).
    std::string assetName() const;

    // App-owned state saved in projects (camera, render, brush, symmetry, ...):
    // the app fills it before saving and applies it after opening; the core
    // keeps it as loaded so CLI and scripts round-trip it unchanged.
    Json projectSettings = Json::object();

    // Unsaved changes: any undoable edit, splat edit or `modified` since the
    // last markClean() (save, open, new scene).
    bool dirty() const {
        return modified || undo.revision() != cleanRevision_ || splatsVersion != cleanSplatsVersion_;
    }
    void markClean() {
        modified = false;
        cleanRevision_ = undo.revision();
        cleanSplatsVersion_ = splatsVersion;
    }

    // Replaces the mesh and clears the edit history.
    void setMesh(Mesh m);
    // Replaces the mesh as an undoable topology change.
    void replaceMeshUndoable(Mesh m, std::string label);

private:
    uint64_t cleanRevision_ = 0, cleanSplatsVersion_ = 0;
};

} // namespace sf
