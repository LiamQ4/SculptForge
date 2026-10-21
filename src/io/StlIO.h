// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// STL (3D printing): binary and ASCII import, binary export. Positions are
// kept as stored (no axis or unit conversion).
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <span>
#include <string>

namespace sf {

// Coincident corners are welded into a connected mesh.
bool loadStl(const std::string& path, Mesh& out, std::string& error);
bool parseStl(std::span<const uint8_t> data, Mesh& out, std::string& error);
// Writes binary STL (every triangle, including hidden ones). Paths are UTF-8.
bool saveStl(const std::string& path, const Mesh& mesh, std::string& error);

} // namespace sf
