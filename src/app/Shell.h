// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pieces of the ZBrush-style shell shared between Shell.cpp, Palettes.cpp and
// the ui:: widgets: the palette table, shelf ids and the button looks.
#pragma once

#include "app/Actions.h"

#include <imgui.h>

#include <string>
#include <string_view>

namespace sf {

class App;

enum ShelfId { kShelfTop = 0, kShelfLeft, kShelfRight, kShelfBottom, kShelfCount };

// Customize-mode drag and drop: ImGui payload "SF_UIITEM" (an item, from a shelf or anywhere else) and
// "SF_PALETTE" (a palette, from its header icon; only trays accept it).
struct UiItemPayload {
    char item[96] = {};
    int fromShelf = -1; // -1: not from a shelf (a palette or picker): dropping it adds a copy
    int fromIndex = -1;
};

struct PaletteDef {
    const char* id;    // "tool", also the UiLayout tray entry
    const char* name;  // "Tool", shown in the palette bar (sorted by it)
    const char* icon;  // ICON_LC_*
    const char* scope; // extra name UI tests find its items under ("SubTools/Append"), or nullptr
    void (App::*draw)();
};
const PaletteDef* palettes(int& count); // alphabetical
const PaletteDef* findPalette(std::string_view id);

// Draws an action as a button in one of the ui::Look styles (0 Label, 1 Icon, 2 IconLabel, 3 Tile).
// Returns true when clicked. ui::ActionButton calls it.
bool drawActionLook(App& app, const Action& action, ImVec2 size, int look);

// Brush type of a "brush.<name>" action id, or -1.
int brushTypeOfAction(std::string_view id);

} // namespace sf
