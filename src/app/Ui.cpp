// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Ui.h"

#include "app/App.h"
#include "app/Shell.h"
#include "app/Theme.h"

#include <algorithm>
#include <cmath>

#include <cstdio>

namespace sf::ui {

// Phase 0 stubs: plain buttons and sliders. The help/console workstream adds Ctrl-hover help and
// customize-mode dragging; the shell workstream adds icon and tile looks.

bool ActionButton(App& app, const char* actionId, ImVec2 size, Look look) {
    const Action* a = app.actions().find(actionId);
    if (!a) return false;
    const bool enabled = !a->enabled || a->enabled();
    ImGui::BeginDisabled(!enabled);
    const bool clicked = drawActionLook(app, *a, size, int(look)); // Shell.cpp: label, icon and tile looks
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
    // ZBrush look: the label and value inside the frame, the position as a thin accent bar along its bottom.
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
    const bool changed = ImGui::SliderFloat(id, v, lo, hi, format, flags);
    ImGui::PopStyleColor(2);
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    float t = hi > lo ? (*v - lo) / (hi - lo) : 0.0f;
    if ((flags & ImGuiSliderFlags_Logarithmic) && lo > 0.0f && hi > lo) t = std::log(*v / lo) / std::log(hi / lo);
    t = std::clamp(t, 0.0f, 1.0f);
    const float r = ImGui::GetStyle().FrameRounding, bar = std::max(2.0f, (b.y - a.y) * 0.12f);
    const float x = a.x + (b.x - a.x) * t;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(x, b.y), theme::u32(theme::accent, ImGui::IsItemActive() ? 0.22f : 0.12f), r,
                      ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(ImVec2(a.x, b.y - bar), ImVec2(x, b.y), theme::u32(ImGui::IsItemActive() ? theme::accentHover : theme::accent),
                      r, ImDrawFlags_RoundCornersBottomLeft);
    return changed;
}

} // namespace sf::ui
