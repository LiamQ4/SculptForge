// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Automatic UV unwrapping (UV > Auto UV) with xatlas (third_party/xatlas, MIT):
// cuts the surface into charts, flattens each one and packs them into the
// 0..1 square with room for the texture painter's island padding, so texture
// painting works on any sculpt. Slow on big meshes (it can take minutes on
// millions of triangles), so the app runs it on a worker thread.
#pragma once

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace sf {

class Mesh;

// Fraction 0..1 of the whole job; return false to cancel. May be called from xatlas' worker threads.
using AutoUvProgress = std::function<bool(float fraction)>;

struct AutoUvResult {
    std::vector<glm::vec2> uvs; // per triangle corner (Mesh::uvs layout), all in [0, 1]
    int charts = 0;
};

// Unwraps `positions` / `triangles` for a `resolution` x `resolution` texture. False with `error` set on
// failure or cancel.
bool computeAutoUv(const std::vector<glm::vec3>& positions, const std::vector<glm::uvec3>& triangles, int resolution,
                   AutoUvResult& out, std::string& error, const AutoUvProgress& progress = {});
// The same for a mesh (its texture size, or 2048 without one).
bool computeAutoUv(const Mesh& mesh, AutoUvResult& out, std::string& error, const AutoUvProgress& progress = {});

} // namespace sf
