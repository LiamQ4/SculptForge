// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/Commands.h"
namespace sf {
void registerCrkwMetaHumanCommands(CommandRegistry& registry);
// Rebuild transient display overrides after loading a project. Source textures stay external.
void restoreCrkwMetaHumanPreviews(Document& doc);
}
