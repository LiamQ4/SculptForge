// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"

#include <string>

namespace sf {

// Paths are UTF-8. Polygons are fan-triangulated; UVs and normals are ignored.
bool loadObj(const std::string& path, Mesh& out, std::string& error);
bool saveObj(const std::string& path, const Mesh& mesh, std::string& error);

} // namespace sf
