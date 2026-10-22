// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Widgets every panel uses, so Ctrl-hover help and UI customization come for free.
#pragma once

#include <imgui.h>

namespace sf {
class App;
}

namespace sf::ui {

enum class Look { Label, Icon, IconLabel, Tile };

// Runs the action on click; toggles draw with the accent when checked(); shows Ctrl-hover help;
// is a Ctrl+Alt drag source in customize mode. Returns true when clicked.
bool ActionButton(App& app, const char* actionId, ImVec2 size = {}, Look look = Look::Label);
// Ctrl-hover help for the last item.
void HelpText(const char* title, const char* shortcut, const char* description, const char* path = nullptr);
void HelpAction(App& app, const char* actionId);
// ZBrush-style slider with its label drawn inside the frame.
bool SliderInside(const char* id, float* v, float lo, float hi, const char* format, ImGuiSliderFlags flags = 0);

} // namespace sf::ui
