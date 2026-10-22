// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygroups and visibility UI: the Tools panel section and the ZBrush
// Ctrl+Shift viewport gestures (click to isolate / hide a group or show all,
// drag a rectangle to hide outside or, with Alt, inside it). Every action
// goes through the command registry (see core/Polygroups.cpp).
#include "app/App.h"

#include "core/Polygroups.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace sf {

namespace {

constexpr float kClickSlopPx = 4.0f;

bool ctrlShiftDown(SDL_Keymod mod) {
    return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0 && (mod & SDL_KMOD_SHIFT) != 0;
}

std::string matrixString(const glm::mat4& m) {
    std::string s;
    char buf[32];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(buf, sizeof buf, "%s%.9g", i ? " " : "", double(m[i / 4][i % 4]));
        s += buf;
    }
    return s;
}

} // namespace

bool App::polygroupMouseDown(float x, float y, SDL_Keymod mod) {
    if (!ctrlShiftDown(mod) || previewLod_ >= 0) return false;
    hideRectActive_ = true;
    hideRectX_ = x;
    hideRectY_ = y;
    return true;
}

void App::polygroupMouseUp() {
    hideRectActive_ = false;
    const bool alt = (inputMods() & SDL_KMOD_ALT) != 0;
    const CanvasRect c = canvasRect();
    if (c.w <= 0.0f || c.h <= 0.0f) return;

    if (std::abs(mouseX_ - hideRectX_) < kClickSlopPx && std::abs(mouseY_ - hideRectY_) < kClickSlopPx) {
        // Click: isolate the group under the cursor (again: invert), Alt hides it,
        // empty canvas shows everything.
        doc_.syncBvh();
        RayHit hit;
        if (!pick(hideRectX_, hideRectY_, hit)) {
            if (doc_.mesh.hasHidden()) runCommand("show_all");
            return;
        }
        const uint32_t group = doc_.mesh.polygroupAt(hit.triangle);
        if (alt) runCommand("hide_polygroup", {{"group", double(group)}});
        else if (polygroupIsolated(doc_.mesh, group)) runCommand("invert_visibility");
        else runCommand("isolate_polygroup", {{"group", double(group)}});
        return;
    }

    // Drag: rectangle in normalised device coordinates (y up).
    auto ndc = [&](float px, float py) { return glm::vec2((px - c.x) / c.w * 2.0f - 1.0f, 1.0f - (py - c.y) / c.h * 2.0f); };
    const glm::vec2 a = ndc(hideRectX_, hideRectY_), b = ndc(mouseX_, mouseY_);
    runCommand("hide_rect", {{"x0", double(a.x)},
                             {"y0", double(a.y)},
                             {"x1", double(b.x)},
                             {"y1", double(b.y)},
                             {"inside", alt},
                             {"view_proj", matrixString(camera_.projection() * camera_.view())}});
}

void App::drawPolygroupOverlay() {
    if (!hideRectActive_) return;
    if (std::abs(mouseX_ - hideRectX_) < kClickSlopPx && std::abs(mouseY_ - hideRectY_) < kClickSlopPx) return;
    // Green keeps the inside, red hides it (Alt).
    const bool alt = (inputMods() & SDL_KMOD_ALT) != 0;
    const ImU32 line = alt ? IM_COL32(255, 110, 110, 230) : IM_COL32(120, 230, 140, 230);
    const ImU32 fill = alt ? IM_COL32(255, 110, 110, 40) : IM_COL32(120, 230, 140, 30);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 p0(std::fmin(hideRectX_, mouseX_), std::fmin(hideRectY_, mouseY_));
    const ImVec2 p1(std::fmax(hideRectX_, mouseX_), std::fmax(hideRectY_, mouseY_));
    dl->AddRectFilled(p0, p1, fill);
    dl->AddRect(p0, p1, line, 0.0f, 0, 1.5f);
}

void App::drawPolygroupsSection() {
    ImGui::Checkbox("Show polygroups", &renderSettings_.showPolygroups);
    ImGui::SameLine();
    const Mesh& m = doc_.mesh;
    const std::array<uint64_t, 6> key = {doc_.undo.revision() + 1, uint64_t(doc_.modified), m.triangles.size(),
                                         m.polygroups.size(), uint64_t(reinterpret_cast<uintptr_t>(m.polygroups.data())),
                                         uint64_t(reinterpret_cast<uintptr_t>(m.hidden.data())) ^ m.hidden.size()};
    if (key != visibleGroupsKey_) {
        visibleGroupsKey_ = key;
        visibleGroups_ = visiblePolygroupCount(m);
    }
    ImGui::TextDisabled("%zu", visibleGroups_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Visible polygroups");

    // Grouping buttons switch the polygroup colours on so the result is visible.
    auto group = [this](const char* command, const Args& args = {}) {
        runCommand(command, args);
        renderSettings_.showPolygroups = true;
    };
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("By normals", ImVec2(half, 0))) group("polygroup_auto_by_normals", {{"angle", double(polygroupAngle_)}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New group wherever the surface turns more than the angle");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##pgangle", &polygroupAngle_, 1.0f, 90.0f, "%.0f deg");
    if (ImGui::Button("By pieces", ImVec2(half, 0))) group("polygroup_by_connectivity");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("One group per disconnected piece");
    ImGui::SameLine();
    if (ImGui::Button("From mask", ImVec2(half, 0))) group("polygroup_from_mask");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Masked triangles become a new group");
    if (ImGui::Button("Merge visible", ImVec2(half, 0))) group("polygroup_merge_visible");
    ImGui::SameLine();
    if (ImGui::Button("Clear groups", ImVec2(half, 0))) runCommand("polygroup_clear");
}

void App::drawVisibilitySection() {
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!doc_.mesh.hasHidden());
    if (ImGui::Button("Show all", ImVec2(half, 0))) runCommand("show_all");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Invert visible", ImVec2(half, 0))) runCommand("invert_visibility");
    ImGui::BeginDisabled(!doc_.mesh.hasHidden());
    if (ImGui::Button("Delete hidden", ImVec2(-1, 0))) runCommand("delete_hidden");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Ctrl+Shift+click a group to isolate it, +Alt to hide it, on the background to show all.\n"
                          "Ctrl+Shift+drag hides outside the rectangle (Alt: inside).");
}

} // namespace sf
