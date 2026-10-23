// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Geometry: subdivision levels, as in ZBrush (Lower Res / Higher Res, SDiv, Del Lower / Del Higher,
// Reconstruct Subdiv, Divide with Smt). The levels themselves live in core/Multires.
#include "app/App.h"
#include "app/Ui.h"

#include "core/Multires.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sf {

void App::drawGeometrySection() {
    const Mesh& m = doc_.mesh;
    const int levels = subdivLevelCount(m), current = subdivCurrentLevel(m);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;

    ui::ActionButton(*this, "geometry.lower_res", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.higher_res", ImVec2(half, 0));

    // SDiv: the level slider (1 = the base cage). Applied when released, as one undo step.
    ImGui::BeginDisabled(levels < 2);
    if (!sdivHeld_) sdivSlider_ = float(current + 1);
    ImGui::SetNextItemWidth(-1);
    char format[32];
    std::snprintf(format, sizeof format, "SDiv %%.0f / %d", std::max(levels, 1));
    ui::SliderInside("##sdiv", &sdivSlider_, 1.0f, float(std::max(levels, 1)), format, ImGuiSliderFlags_AlwaysClamp);
    sdivHeld_ = ImGui::IsItemActive();
    sdivSlider_ = std::round(sdivSlider_);
    if (ImGui::IsItemDeactivatedAfterEdit() && int(sdivSlider_) - 1 != current)
        runCommand("subdiv_level", {{"level", double(sdivSlider_)}});
    ImGui::EndDisabled();
    ui::HelpText("SDiv", nullptr,
                 "The subdivision level to sculpt at: 1 is the base cage, the highest has the finest detail. "
                 "D / Shift+D step up / down. Detail is kept on every level.",
                 "Tool > Geometry");

    ui::ActionButton(*this, "geometry.del_lower", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.del_higher", ImVec2(half, 0));
    ui::ActionButton(*this, "geometry.reconstruct", ImVec2(-1, 0));

    // Divide with its Smt toggle beside it, as in ZBrush.
    const float smtW = ImGui::CalcTextSize("Smt").x + 2.0f * style.FramePadding.x + 6.0f * uiScale_;
    ui::ActionButton(*this, "geometry.divide", ImVec2(ImGui::GetContentRegionAvail().x - smtW - style.ItemSpacing.x, 0),
                     ui::Look::IconLabel);
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.smt", ImVec2(-1, 0));

    if (levels) {
        char info[96];
        std::snprintf(info, sizeof info, "SDiv %d of %d  -  base %zu, top %zu triangles", current + 1, levels,
                      subdivLevelTriangles(m, 0), subdivLevelTriangles(m, levels - 1));
        ImGui::TextDisabled("%s", info);
    } else {
        ImGui::TextDisabled("No subdivision levels - Divide adds them");
    }
    ImGui::Separator();
}

} // namespace sf
