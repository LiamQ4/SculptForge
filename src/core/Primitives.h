// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"

namespace sf {

// Returned meshes have topology and normals built.
Mesh makeIcosphere(int subdivisions, float radius = 1.0f);
Mesh makePlane(int resolution, float size = 2.0f);
// Loop subdivision (smooth; boundaries follow the cubic B-spline rule).
Mesh loopSubdivide(const Mesh& mesh);

} // namespace sf
