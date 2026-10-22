// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One registry of user-facing actions drives the shelves, palettes, Ctrl-hover
// help, the command palette, keyboard shortcuts and UI customization. Every
// CommandRegistry command without required parameters is also registered as
// "cmd.<name>", so anything scriptable is reachable from the UI.
#pragma once

#include "core/Json.h"

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

struct Action {
    std::string id;          // "geometry.subdivide", "ui.lightbox", "brush.clay", "view.wireframe"
    std::string label;       // "Subdivide"
    const char* icon = nullptr; // ICON_LC_* or nullptr
    std::string description; // Ctrl-hover help; defaults to the command's description
    std::string palette;     // "Tool > Geometry" (help footer, command palette)
    std::string shortcut;    // default chord: "Ctrl+D", "Shift+F", ","; "" = none
    std::string command;     // CommandRegistry name when the action runs a command
    std::function<void()> run;
    std::function<bool()> checked; // toggles; null = not a toggle
    std::function<bool()> enabled; // null = always
};

class ActionRegistry {
public:
    void add(Action a); // replaces by id
    const Action* find(std::string_view id) const;
    const std::vector<Action>& all() const { return actions_; }
    // Runs the action bound to this chord; true if consumed. Callers skip it while ImGui wants the keyboard.
    bool handleKey(SDL_Keycode key, SDL_Keymod mods);
    std::string shortcutText(std::string_view id) const; // effective chord after user overrides
    void setShortcut(std::string_view id, std::string chord); // user override ("" clears)
    Json overridesToJson() const;
    void overridesFromJson(const Json& j);
    void touch(std::string_view id); // most-recently-used, for the command palette
    const std::vector<std::string>& recent() const { return recent_; }

private:
    std::vector<Action> actions_;
    std::vector<std::pair<std::string, std::string>> overrides_; // id -> chord
    std::vector<std::string> recent_;
};

} // namespace sf
