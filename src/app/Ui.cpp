// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Ui.h"

#include "app/App.h"

#include <cstdio>

namespace sf::ui {

// Phase 0 stubs: plain buttons and sliders. The help/console workstream adds Ctrl-hover help and
// customize-mode dragging; the shell workstream adds icon and tile looks.

bool ActionButton(App& app, const char* actionId, ImVec2 size, Look) {
    const Action* a = app.actions().find(actionId);
    if (!a) return false;
    const bool enabled = !a->enabled || a->enabled();
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Button((a->label + "##" + a->id).c_str(), size);
    ImGui::EndDisabled();
    if (clicked && a->run) {
        app.actions().touch(a->id);
        a->run();
    }
    return clicked;
}

void HelpText(const char*, const char*, const char*, const char*) {}

void HelpAction(App&, const char*) {}

bool SliderInside(const char* id, float* v, float lo, float hi, const char* format, ImGuiSliderFlags flags) {
    return ImGui::SliderFloat(id, v, lo, hi, format, flags);
}

} // namespace sf::ui
