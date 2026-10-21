// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenUSD interchange without the OpenUSD SDK (keeps builds small and
// portable). Writes text USD (.usda) and USDZ packages (.usdz, for AR Quick
// Look / Vision Pro, Omniverse, Houdini Solaris, Blender, Maya, Unreal):
//  - the sculpt as a UsdGeomMesh with a UsdPreviewSurface clay material
//  - sculpt layers as UsdSkel blend shapes, current weights on a SkelAnimation
//  - Gaussian splats as a UsdVol ParticleField3DGaussianSplat prim (OpenUSD 26.03+)
//
// Reads .usda files and .usdz packages whose root layer is text: meshes (any
// polygon counts, xform ops, Z-up stages), blend shapes and Gaussian splats.
// Binary .usdc layers and external references are not resolved yet.
#pragma once

#include "core/Mesh.h"
#include "core/Splats.h"

#include <string>

namespace sf {

// Either pointer may be null (or empty) but not both. Format by extension.
bool saveUsd(const std::string& path, const Mesh* mesh, const SplatCloud* splats, const std::string& name,
             std::string& error);

// Fills whichever of `mesh` / `splats` the file contains (the other is left
// empty). Fails if it contains neither.
bool loadUsd(const std::string& path, Mesh& mesh, SplatCloud& splats, std::string& error);

// A valid USD prim name from arbitrary text ("Smile 2" -> "Smile_2").
std::string usdIdentifier(const std::string& text);

} // namespace sf
