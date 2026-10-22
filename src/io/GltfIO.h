// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"
#include "core/Splats.h"
#include "io/MeshObject.h"

#include <string>

namespace sf {

// Writes a binary glTF 2.0 (.glb). The base shape (all layers at zero) is the
// mesh; each sculpt layer becomes a morph target with its current weight as
// the default, and target names stored in mesh.extras.targetNames (the
// convention read by Blender, Unity, Unreal, Godot and three.js).
bool saveGlb(const std::string& path, const Mesh& mesh, const std::string& name, std::string& error);
// One node and mesh per object (SubTool), each with its own morph targets;
// with several, the scene is called `sceneName`.
bool saveGlb(const std::string& path, const std::vector<MeshObjectRef>& objects, const std::string& sceneName,
             std::string& error);

// Reads .glb or .gltf (embedded/data-URI or external buffers). All triangle
// primitives in the default scene are merged with their node transforms
// applied, duplicate vertices along UV/normal seams are welded so the mesh can
// be sculpted, and morph targets become sculpt layers.
// With `splats`, a KHR_gaussian_splatting primitive is read into it too (the
// first one found); a file with only splats then succeeds with an empty mesh.
bool loadGltf(const std::string& path, Mesh& out, std::string& error, SplatCloud* splats = nullptr);
// The same, but each node with a mesh is its own object (named after the node,
// else its mesh), in world space. A splat-only file gives no objects.
bool loadGltf(const std::string& path, std::vector<MeshObject>& out, std::string& error, SplatCloud* splats = nullptr);

// Writes Gaussian splats as a KHR_gaussian_splatting POINTS primitive (with a
// COLOR_0 point-cloud fallback for viewers without splat support).
bool saveSplatGlb(const std::string& path, const SplatCloud& splats, std::string& error);

} // namespace sf
