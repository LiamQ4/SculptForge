// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/Commands.h"
#include <span>
namespace sf {
struct CrkwMaskPreset { const char* name; float cx, cy, rx, ry; int rings, feather; bool paired; };
std::span<const CrkwMaskPreset> crkwMaskPresets();
const Json& crkwMaskSettings(const Mesh& mesh);
void registerCrkwMaskManagerCommands(CommandRegistry& registry);
}
