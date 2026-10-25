// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Masking: Ctrl mask strokes, MaskLasso / MaskRect selections (the shape is
// tracked and drawn by Selection.cpp), the Ctrl + background gestures and the
// "Masking" section of the Tools panel. The operations themselves are the
// mask_* commands (core/Masking.cpp).
#include "app/App.h"
#include "app/Ui.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr float kMaskDragThresholdPx = 4.0f; // movement that turns a Ctrl+click into a drag

bool ctrlDown(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }

bool anyMasked(const Mesh& mesh) {
    return std::any_of(mesh.mask.begin(), mesh.mask.end(), [](float m) { return m > 0.0f; });
}

} // namespace

void App::beginMaskStroke(float x, float y) {
    // Masks are not layered, so the stroke always targets the base (its weight is never 0).
    // Ctrl+Alt erases, and so does the pen's eraser end (set to Invert or Erase mask).
    strokeInvert_ = (inputMods() & SDL_KMOD_ALT) != 0 ||
                    (erasing() && (eraserAction() == PenAction::Invert || eraserAction() == PenAction::EraseMask));
    if (!doc_.sculptor.beginStroke(doc_.mesh, strokeInvert_ ? "Unmask" : "Mask", -1)) return;
    strokeMask_ = true;
    strokePaint_ = false; // mask strokes never paint or sculpt
    strokeSculpt_ = true;
    paintSnapshot_.reset();
    strokeBrush_ = BrushType::Standard;
    haveLastDab_ = false;
    drag_ = Drag::Stroke;
    lastX_ = x;
    lastY_ = y;
    residual_ = 0.0f;
    lazyMouse_.reset(glm::vec2(x, y));
    // ZBrush: a Ctrl+click on the model blurs the mask (Ctrl+Alt+click sharpens it); only a drag paints. The
    // pen's eraser end set to Erase mask paints straight away.
    maskClickPending_ = ctrlDown(inputMods());
    maskClickX_ = x;
    maskClickY_ = y;
    if (!maskClickPending_) firstDab(x, y);
}

void App::maskClickRelease() {
    maskClickPending_ = false;
    const bool sharpen = strokeInvert_;
    endStroke(); // nothing was dabbed: no undo step
    if (anyMasked(doc_.mesh)) runCommand(sharpen ? "mask_sharpen" : "mask_blur");
}

// ZBrush convention: Ctrl+drag on the background clears the mask. With nothing masked the SculptForge input
// style zooms instead, as it did before masking existed; the ZBrush style does nothing.
void App::maskCanvasMove(float x, float y) {
    if (maskCanvasMoved_) return;
    if (std::hypot(x - maskCanvasX_, y - maskCanvasY_) < kMaskDragThresholdPx) return;
    maskCanvasMoved_ = true;
    if (anyMasked(doc_.mesh)) runCommand("mask_clear");
    else if (!zbrushStyle()) drag_ = Drag::Zoom;
}

void App::maskCanvasRelease() {
    if (!maskCanvasMoved_) runCommand("mask_invert"); // Ctrl+click on the background
}

// MaskLasso / MaskRect: Ctrl+drag draws the shape, starting on the model or the background.
bool App::maskSelectMouseDown(float x, float y, SDL_Keymod mod) {
    const bool ctrl = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    if (!ctrl || (mod & SDL_KMOD_SHIFT) || maskShape_ == SelectShape::Pen || previewLod_ >= 0) return false;
    beginSelection(x, y, SelectTarget::Mask, maskShape_);
    return true;
}

void App::maskSelectMouseUp() {
    const bool alt = (inputMods() & SDL_KMOD_ALT) != 0;
    if (!selectMoved_) {
        // A click on the background inverts the mask, as with MaskPen; on the model it does nothing.
        doc_.syncBvh();
        RayHit hit;
        if (!alt && !pick(selectPath_.front().x, selectPath_.front().y, hit)) runCommand("mask_invert");
        return;
    }
    // Mask the inside, or unmask it with Alt (Mask Strength sets how much).
    runSelection("mask_rect", "mask_lasso", {{"unmask", alt}, {"strength", double(maskStrength_)}});
}

void App::drawMaskingSection() {
    ImGui::Checkbox("Show mask", &renderSettings_.showMask);
    ImGui::SameLine();
    ImGui::TextDisabled(maskShape_ == SelectShape::Pen ? "(Ctrl paints, Ctrl+Alt erases)"
                                                       : "(Ctrl masks, Ctrl+Alt unmasks)");
    ui::Segmented(*this, {"mask.pen", "mask.lasso", "mask.rect"});
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##maskstrength", &maskStrength_, 0.01f, 1.0f, "Mask Strength %.2f");

    const ImGuiStyle& style = ImGui::GetStyle();
    const float third = (ImGui::GetContentRegionAvail().x - 2 * style.ItemSpacing.x) / 3.0f;
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Clear##mask", ImVec2(third, 0))) runCommand("mask_clear");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unmask everything (MaskPen: Ctrl + drag background)");
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
    if (ImGui::Button("By polygroup", ImVec2(half, 0)))
        runCommand("mask_by_polygroup", {{"group", double(maskGroup_)}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mask one polygroup (replaces the mask)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputInt("##maskgroup", &maskGroup_)) maskGroup_ = std::max(0, maskGroup_);
}

} // namespace sf
