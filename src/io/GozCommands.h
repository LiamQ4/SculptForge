// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ZBrush interoperability commands: goz_import, goz_export, goz_send,
// goz_receive and export_stl. Registered by CommandRegistry's constructor.
#pragma once

#include "core/Commands.h"

namespace sf {

void registerGozCommands(CommandRegistry& registry);

} // namespace sf
