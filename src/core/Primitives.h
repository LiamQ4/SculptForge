// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"

namespace sf {

// Returned meshes have topology and normals built.
Mesh makeIcosphere(int subdivisions, float radius = 1.0f);
Mesh makePlane(int resolution, float size = 2.0f);
// Closed cube from -size/2 to size/2 with `resolution` cells per edge (welded, one polygroup per side).
Mesh makeCube(int resolution, float size = 2.0f);
// Loop subdivision (smooth; boundaries follow the cubic B-spline rule).
// Layers are subdivided with the same stencils; mask/colours/UVs are
// interpolated linearly and polygroups/hidden flags are inherited.
Mesh loopSubdivide(const Mesh& mesh);

} // namespace sf
