// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Masking: Ctrl mask strokes, the Ctrl + background gestures and the
// "Masking" section of the Tools panel. The operations themselves are the
// mask_* commands (core/Masking.cpp).
#include "app/App.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr float kMaskDragThresholdPx = 4.0f; // movement that turns a Ctrl+click into a drag

bool anyMasked(const Mesh& mesh) {
    return std::any_of(mesh.mask.begin(), mesh.mask.end(), [](float m) { return m > 0.0f; });
}

} // namespace

void App::beginMaskStroke(float x, float y) {
    // Masks are not layered, so the stroke always targets the base (its weight is never 0).
    strokeInvert_ = (SDL_GetModState() & SDL_KMOD_ALT) != 0; // Ctrl+Alt erases
    if (!doc_.sculptor.beginStroke(doc_.mesh, strokeInvert_ ? "Unmask" : "Mask", -1)) return;
    strokeMask_ = true;
    strokeBrush_ = BrushType::Standard;
    haveLastDab_ = false;
    drag_ = Drag::Stroke;
    lastX_ = x;
    lastY_ = y;
    residual_ = 0.0f;
    lazyMouse_.reset(glm::vec2(x, y));
    dabAt(x, y);
}

// ZBrush convention: Ctrl+drag on the background clears the mask. With
// nothing masked the drag zooms instead, as it did before masking existed.
void App::maskCanvasMove(float x, float y) {
    if (maskCanvasMoved_) return;
    if (std::hypot(x - maskCanvasX_, y - maskCanvasY_) < kMaskDragThresholdPx) return;
    maskCanvasMoved_ = true;
    if (anyMasked(doc_.mesh)) runCommand("mask_clear");
    else drag_ = Drag::Zoom;
}

void App::maskCanvasRelease() {
    if (!maskCanvasMoved_) runCommand("mask_invert"); // Ctrl+click on the background
}

void App::drawMaskingSection() {
    ImGui::SeparatorText("Masking");
    ImGui::Checkbox("Show mask", &renderSettings_.showMask);
    ImGui::SameLine();
    ImGui::TextDisabled("(Ctrl paints, Ctrl+Alt erases)");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##maskstrength", &maskStrength_, 0.01f, 1.0f, "mask strength %.2f");

    const ImGuiStyle& style = ImGui::GetStyle();
    const float third = (ImGui::GetContentRegionAvail().x - 2 * style.ItemSpacing.x) / 3.0f;
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Clear##mask", ImVec2(third, 0))) runCommand("mask_clear");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unmask everything (Ctrl + drag background)");
    ImGui::SameLine();
    if (ImGui::Button("Invert##mask", ImVec2(third, 0))) runCommand("mask_invert");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl + click background");
    ImGui::SameLine();
    if (ImGui::Button("All##mask", ImVec2(third, 0))) runCommand("mask_all");

    const Args passes = {{"iterations", double(maskIterations_)}};
    if (ImGui::Button("Blur##mask", ImVec2(half, 0))) runCommand("mask_blur", passes);
    ImGui::SameLine();
    if (ImGui::Button("Sharpen##mask", ImVec2(half, 0))) runCommand("mask_sharpen", passes);
    if (ImGui::Button("Grow##mask", ImVec2(half, 0))) runCommand("mask_grow", passes);
    ImGui::SameLine();
    if (ImGui::Button("Shrink##mask", ImVec2(half, 0))) runCommand("mask_shrink", passes);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##maskpasses", &maskIterations_, 1, 20, "passes %d");

    if (ImGui::Button("By cavity", ImVec2(half, 0)))
        runCommand("mask_by_cavity", {{"strength", double(maskStrength_)}, {"threshold", double(cavityThreshold_)}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mask creases and folds (replaces the mask; uses the mask strength)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##cavitythreshold", &cavityThreshold_, -0.2f, 0.5f, "threshold %.2f");
    if (ImGui::Button("By polygroup", ImVec2(half, 0))) runCommand("mask_by_polygroup", {{"index", double(maskGroup_)}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mask one polygroup (replaces the mask)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputInt("##maskgroup", &maskGroup_)) maskGroup_ = std::max(0, maskGroup_);
}

} // namespace sf
