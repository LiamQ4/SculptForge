// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"

#include <vector>

namespace sf {

// Quadric-error-metric edge-collapse decimation (Garland & Heckbert), with
// boundary preservation, normal-flip and manifold (link condition) checks.
// Collapses keep one endpoint, so output vertices are a subset of the input's
// and sculpt layers / blend shapes, mask and colours carry over exactly;
// surviving triangles keep their polygroup and hidden flag, and corner UVs
// follow the kept vertex.
Mesh decimate(const Mesh& src, size_t targetTriangles);

// LOD chain: lods[0] is a copy of `src`, each following level has
// `ratio` times the triangles of the previous one.
std::vector<Mesh> generateLods(const Mesh& src, int levels, float ratio);

} // namespace sf
