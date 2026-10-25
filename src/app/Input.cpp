// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ZBrush feel (docs/zbrush-parity.md): ZBrush's keys that SculptForge lacked (U Z Intensity, I RGB
// Intensity, O Focal Shift, Space QuickMenu, L LazyMouse, V switch colour, Ctrl+A / Ctrl+Shift+A / Ctrl+I /
// Ctrl+H for the mask, Ctrl+W group masked), Preferences > Input with the ZBrush and SculptForge key styles,
// the QuickMenu itself and Shift-constrained straight strokes. The mouse gestures live with the rest of the
// input in App.cpp (roll, Ctrl+click blur / sharpen in PanelMasking.cpp).
#include "app/App.h"
#include "app/Pickers.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace sf {

namespace {

constexpr double kQuickMenuTapSeconds = 0.25; // Space released sooner: a tap, the QuickMenu stays open

bool anyMasked(const Mesh& mesh) {
    return std::any_of(mesh.mask.begin(), mesh.mask.end(), [](float m) { return m > 0.0f; });
}

// The brush's action id, as the shell registers it ("brush.claybuildup").
std::string brushId(BrushType t) {
    std::string id = "brush.";
    for (const char* c = brushName(t); *c; ++c)
        if (*c != ' ') id += char(std::tolower(static_cast<unsigned char>(*c)));
    return id;
}

} // namespace

void App::registerInputActions() {
    auto add = [this](const char* id, const char* label, const char* icon, const char* palette, const char* shortcut,
                      const char* description, std::function<void()> run, std::function<bool()> checked = nullptr,
                      std::function<bool()> enabled = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = palette;
        a.shortcut = shortcut;
        a.description = description;
        a.run = std::move(run);
        a.checked = std::move(checked);
        a.enabled = std::move(enabled);
        actions_.add(std::move(a));
    };

    // The sliders at the cursor (S is Draw Size, in LightBox.cpp).
    add("ui.strength_popup", "Z Intensity at cursor", ICON_LC_CIRCLE_DOT, "Draw", "U",
        "A Z Intensity slider under the cursor: drag it, or type a number (1-100) and press Enter. Hold Shift (or "
        "pick Smooth) to set Smooth's own Z Intensity.",
        [this] { PickerUi::sizePopup(*this, PickerUi::Slider::ZIntensity); });
    add("ui.rgb_popup", "RGB Intensity at cursor", ICON_LC_PALETTE, "Draw", "I",
        "An RGB Intensity slider under the cursor: how strongly painting colours the surface (1-100).",
        [this] { PickerUi::sizePopup(*this, PickerUi::Slider::RgbIntensity); });
    add("ui.focal_popup", "Focal Shift at cursor", ICON_LC_CIRCLE_DASHED, "Draw", "O",
        "A Focal Shift slider under the cursor: below 0 a broad, hard brush tip, above 0 a soft point (-100..100). The "
        "inner ring of the brush cursor shows it.",
        [this] { PickerUi::sizePopup(*this, PickerUi::Slider::FocalShift); });
    add("ui.quickmenu", "QuickMenu", ICON_LC_LAYOUT_GRID, "Help", "Space",
        "ZBrush's QuickMenu under the cursor: brush, stroke, alpha, material, colour, Draw Size, Focal Shift and the "
        "intensities. Hold Space and let go to close it, or tap Space to keep it open.",
        [this] { openQuickMenu(); }, [this] { return quickMenu_.open; });

    // Stroke and colour.
    add("stroke.lazy", "LazyMouse", ICON_LC_SPLINE, "Stroke", "L",
        "LazyMouse on or off: the brush trails the cursor on a string, for steady lines.",
        [this] { strokeType_ = strokeType_ == StrokeType::LazyMouse ? StrokeType::Dots : StrokeType::LazyMouse; },
        [this] { return strokeType_ == StrokeType::LazyMouse; });
    add("color.switch", "Switch Color", ICON_LC_ARROW_LEFT_RIGHT, "Color", "V",
        "Swap the main and the secondary colour.", [this] { std::swap(doc_.paintColor, secondaryColor_); });

    // Masking (ZBrush Tool > Masking keys).
    add("mask.all", "Mask All", ICON_LC_SQUARE, "Tool > Masking", "Ctrl+A", "Mask the whole visible SubTool.",
        [this] { runCommand("mask_all"); });
    add("mask.clear", "Clear Mask", ICON_LC_ERASER, "Tool > Masking", "Ctrl+Shift+A",
        "Unmask everything (also: Ctrl+drag on the background).", [this] { runCommand("mask_clear"); });
    add("mask.invert", "Invert Mask", ICON_LC_FLIP_HORIZONTAL_2, "Tool > Masking", "Ctrl+I",
        "Mask what is unmasked and unmask what is masked (also: Ctrl+click the background).",
        [this] { runCommand("mask_invert"); });
    actions_.setDefaultShortcut("view.mask", "Ctrl+H"); // View Mask
    add("polygroup.group_masked", "Group Masked", ICON_LC_SHAPES, "Tool > Polygroups", "Ctrl+W",
        "Turn the masked part into a new polygroup and clear the mask (ZBrush Ctrl+W). With nothing masked, "
        "everything visible becomes one group.",
        [this] { groupMasked(); });
    actions_.setDefaultShortcut("config.save", "Ctrl+Shift+Alt+I");

    // Preferences > Input: the key style.
    add("input.style_zbrush", "ZBrush", nullptr, "Preferences > Input", "",
        "ZBrush's keys and gestures: I is RGB Intensity, 9 is QuickSave, the number keys don't pick brushes, and "
        "Ctrl+drag on the background only clears the mask.",
        [this] {
            uiPrefs_.inputStyle = 0;
            applyInputStyle();
        },
        [this] { return uiPrefs_.inputStyle == 0; });
    add("input.style_sculptforge", "SculptForge", nullptr, "Preferences > Input", "",
        "SculptForge's own keys from before 0.7: 1-9 and 0 pick the first ten brushes, I is Z Intensity (as is U), "
        "and Ctrl+drag on the background zooms when nothing is masked.",
        [this] {
            uiPrefs_.inputStyle = 1;
            applyInputStyle();
        },
        [this] { return uiPrefs_.inputStyle == 1; });
    applyInputStyle();
}

void App::applyInputStyle() {
    const bool sf = !zbrushStyle();
    for (int i = 0; i < int(BrushType::Count) && i < 10; ++i)
        actions_.setDefaultShortcut(brushId(BrushType(i)), sf ? (i < 9 ? std::to_string(i + 1) : "0") : "");
    actions_.setDefaultShortcut("file.quicksave", sf ? "" : "9");
    actions_.setDefaultShortcut("ui.strength_popup", sf ? "U / I" : "U");
    actions_.setDefaultShortcut("ui.rgb_popup", sf ? "" : "I");
}

void App::drawInputSection() {
    ImGui::TextDisabled("Style");
    ImGui::SameLine();
    ui::Segmented(*this, {"input.style_zbrush", "input.style_sculptforge"});
    ImGui::TextDisabled("Rotation");
    ImGui::SameLine();
    int rotation = uiPrefs_.turntable ? 1 : 0;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##rotation_input", &rotation, "Free (ZBrush)\0Turntable\0")) uiPrefs_.turntable = rotation == 1;
    ui::HelpText("Rotation", nullptr,
                 "Free: the model turns about the screen's axes and can roll over the top, as in ZBrush. Turntable: it "
                 "stays upright, as in Blender or Maya.",
                 "Preferences > Input");
}

void App::drawZIntensitySlider(const char* id) {
    const bool smooth = activeBrush() == BrushType::Smooth && drag_ != Drag::Stroke;
    float& value = smooth ? smoothStrength_ : strength_;
    float shown = value * 100.0f;
    if (ui::SliderInside(id, &shown, 1.0f, 100.0f, smooth ? "Smooth Z Intensity %.0f" : "Z Intensity %.0f"))
        value = std::clamp(shown / 100.0f, 0.01f, 1.0f);
    ui::HelpText(smooth ? "Smooth Z Intensity" : "Z Intensity", "U",
                 smooth ? "How strongly Smooth works. Smooth keeps its own value, as in ZBrush: hold Shift to see and "
                          "set it. Ctrl+click to type."
                        : "How strongly each dab changes the surface (1-100). Holding Shift shows Smooth's own value. "
                          "Ctrl+click to type.",
                 "Draw");
}

// ---------------------------------------------------------------------------
// Ctrl+W
// ---------------------------------------------------------------------------

void App::groupMasked() {
    if (anyMasked(doc_.mesh)) {
        runCommand("polygroup_from_mask");
        if (lastCommandOk_) {
            const std::string message = status_;
            runCommand("mask_clear");
            status_ = message;
        }
    } else {
        runCommand("polygroup_merge_visible");
    }
    if (lastCommandOk_) renderSettings_.showPolygroups = true; // like the panel's buttons: show the result
}

// ---------------------------------------------------------------------------
// Straight strokes
// ---------------------------------------------------------------------------

glm::vec2 App::constrainStroke(float x, float y) {
    if (straight_.shiftAtStart || !(inputMods() & SDL_KMOD_SHIFT)) {
        straight_.active = false;
        return {x, y};
    }
    if (!straight_.active) {
        straight_.active = true;
        straight_.origin = glm::vec2(lastX_, lastY_); // the line starts where the stroke is when Shift goes down
    }
    const glm::vec2 d = glm::vec2(x, y) - straight_.origin;
    if (glm::length(d) < 1e-3f) return straight_.origin;
    constexpr float kStep = 3.14159265f / 4.0f; // 45 degrees
    const float angle = std::round(std::atan2(d.y, d.x) / kStep) * kStep;
    const glm::vec2 dir(std::cos(angle), std::sin(angle));
    return straight_.origin + dir * std::max(0.0f, glm::dot(d, dir));
}

// ---------------------------------------------------------------------------
// QuickMenu (Space)
// ---------------------------------------------------------------------------

void App::openQuickMenu() {
    if (quickMenu_.open && !quickMenu_.keyHeld) { // tapped open, tapped again: close
        quickMenu_.open = false;
        return;
    }
    quickMenu_.open = true;
    quickMenu_.at = ImVec2(mouseX_, mouseY_);
    quickMenu_.openedAt = ImGui::GetTime();
    quickMenu_.keyHeld = true;
    quickMenu_.untilLeave = false;
    quickMenu_.openedFrame = ImGui::GetFrameCount();
}

void App::quickMenuKeyUp() {
    if (!quickMenu_.keyHeld) return;
    quickMenu_.keyHeld = false;
    if (!quickMenu_.open) return;
    // Held: let go to close, as in ZBrush, unless the pointer is on the menu (then it stays until it leaves).
    const bool tap = ImGui::GetTime() - quickMenu_.openedAt < kQuickMenuTapSeconds;
    ImGuiWindow* w = ImGui::FindWindowByName("QuickMenu##quickmenu");
    const bool over = w && ImGui::IsMouseHoveringRect(w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y), false);
    if (!tap && !over) quickMenu_.open = false;
    quickMenu_.untilLeave = !tap && over;
}

void App::drawQuickMenu() {
    if (!quickMenu_.open) return;
    if (pickerOpen() && ImGui::GetFrameCount() > quickMenu_.openedFrame + 1) { // a tile opened its picker
        quickMenu_.open = false;
        return;
    }
    const float s = uiScale_;
    const float tile = 56.0f * s;
    const float width = 4 * tile + 3 * ImGui::GetStyle().ItemSpacing.x + 150.0f * s;
    ImGui::SetNextWindowPos(ImVec2(quickMenu_.at.x - width * 0.5f, quickMenu_.at.y - tile * 0.5f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(37, 37, 40, 245));
    bool close = false;
    if (ImGui::Begin("QuickMenu##quickmenu", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::BeginGroup();
        drawBrushTile("##qm_brush", ImVec2(tile, tile));
        ImGui::SameLine();
        drawStrokeTile("##qm_stroke", ImVec2(tile, tile));
        ImGui::SameLine();
        drawAlphaTile("##qm_alpha", ImVec2(tile, tile));
        ImGui::SameLine();
        drawMaterialTile("##qm_material", ImVec2(tile, tile));
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        const float sw = 150.0f * s;
        ImGui::SetNextItemWidth(sw);
        ui::SliderInside("##qm_drawsize", &radiusPx_, 2.0f, 800.0f, "Draw Size %.0f", ImGuiSliderFlags_Logarithmic);
        ui::HelpText("Draw Size", "S", "Brush radius in screen pixels.", "QuickMenu");
        float focal = focalShift_ * 100.0f;
        ImGui::SetNextItemWidth(sw);
        if (ui::SliderInside("##qm_focal", &focal, -100.0f, 100.0f, "Focal Shift %.0f")) focalShift_ = focal / 100.0f;
        ui::HelpText("Focal Shift", "O", "Below 0: a broad, hard tip. Above 0: a soft point.", "QuickMenu");
        const bool smooth = activeBrush() == BrushType::Smooth;
        float& zValue = smooth ? smoothStrength_ : strength_;
        float z = zValue * 100.0f;
        ImGui::SetNextItemWidth(sw);
        if (ui::SliderInside("##qm_z", &z, 1.0f, 100.0f, smooth ? "Smooth Z Intensity %.0f" : "Z Intensity %.0f"))
            zValue = z / 100.0f;
        ui::HelpText("Z Intensity", "U", "How strongly the brush changes the surface.", "QuickMenu");
        float rgb = rgbIntensity_ * 100.0f;
        ImGui::SetNextItemWidth(sw);
        if (ui::SliderInside("##qm_rgb", &rgb, 1.0f, 100.0f, "RGB Intensity %.0f")) rgbIntensity_ = rgb / 100.0f;
        ui::HelpText("RGB Intensity", "I", "How strongly painting colours the surface.", "QuickMenu");
        ImGui::EndGroup();
        ui::Segmented(*this, {"draw.zadd", "draw.zsub"}, 2 * tile + ImGui::GetStyle().ItemSpacing.x);
        ImGui::SameLine();
        ui::Segmented(*this, {"draw.sculpt", "draw.paint"}, 2 * tile + ImGui::GetStyle().ItemSpacing.x);
        ImGui::SameLine();
        ui::ActionButton(*this, "draw.rgb");
        ImGui::SameLine();
        ui::ActionButton(*this, "transform.symmetry_x");
        const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (clicked && !hovered && ImGui::GetFrameCount() > quickMenu_.openedFrame + 1) close = true;
        // Space was let go while pointing at it: moving off it closes it.
        if (quickMenu_.untilLeave && !hovered && !ImGui::IsAnyItemActive()) close = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close = true;
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (close) quickMenu_.open = false;
}

} // namespace sf
