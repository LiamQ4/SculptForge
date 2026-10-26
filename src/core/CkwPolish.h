// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glm/vec3.hpp>
#include <vector>

namespace sf {
class CommandRegistry;
class Mesh;
struct CommandResult;
struct Document;

void registerCkwPolishCommands(CommandRegistry& registry);
void registerCrkwMeshSharpenCommands(CommandRegistry& registry);

// Shared by the CRKW surface filters. Per vertex, how much a filter may move it: 0 when hidden, else 1 - mask.
std::vector<float> crkwInfluence(const Mesh& m);
// Commits doc.mesh.positions (moved from `original`) as one undo step on the active sculpt layer: cancels and
// restores `original` if anything went non-finite, and reports "<label> applied" / "<label>: surface unchanged".
CommandResult crkwCommitPositions(Document& doc, const std::vector<glm::vec3>& original, float layerWeight,
                                  const char* label);
}
