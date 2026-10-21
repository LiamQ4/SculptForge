// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Binary FBX (7.4) export, written from the publicly documented file layout
// (no Autodesk SDK, so it can ship under the GPL). FBX is still what Unreal
// Engine imports most completely:
//  - the sculpt as a triangle mesh with per-corner normals, smoothing and a
//    material slot
//  - sculpt layers as blend shapes (Unreal: morph targets on a skeletal mesh)
//  - a LOD chain as an FBX LOD group (Unreal: LOD0..N of one static mesh)
// Import uses ufbx (third_party/ufbx).
#pragma once

#include "core/Mesh.h"

#include <string>
#include <vector>

namespace sf {

struct FbxExportOptions {
    float scale = 1.0f;                  // applied to positions and layer offsets
    std::string materialName = "M_Clay"; // becomes the engine's material slot
    glm::vec3 color{0.80f, 0.55f, 0.45f};
    bool layers = true;                  // export sculpt layers as blend shapes (on LOD0)
};

// `lods[0]` is the full-resolution mesh. With more than one entry the meshes
// are grouped under an FBX LOD group named `name`. Units are centimetres
// (FBX default) and the scene is Y-up; importers convert to their own axes.
bool saveFbx(const std::string& path, const std::string& name, const std::vector<const Mesh*>& lods,
             const FbxExportOptions& options, std::string& error);

// Reads .fbx (binary or ASCII, any version) via ufbx: all visible meshes are
// merged in world space (Y-up, 1 unit = 1 m, matching saveFbx with scale 100),
// only LOD0 of LOD groups is taken, and blend shapes become sculpt layers.
bool loadFbx(const std::string& path, Mesh& out, std::string& error);

} // namespace sf
