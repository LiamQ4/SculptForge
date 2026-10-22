// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brush options panel (stroke type, lazy mouse, spray, alpha) and the
// stroke handling of the SnakeHook brush and the DragRect stroke.
#include "app/App.h"
#include "app/Ui.h"

#include <iterator>

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

const SDL_DialogFileFilter kAlphaFilters[] = {{"Images", "png;jpg;jpeg;bmp;tga;psd;gif"}, {"All files", "*"}};

} // namespace

void SDLCALL App::onAlphaChosen(void* userdata, const char* const* files, int) {
    if (!files || !files[0]) return;
    auto* app = static_cast<App*>(userdata);
    std::lock_guard lock(app->alphaMutex_);
    app->pendingAlphaPath_ = files[0];
}

glm::vec3 App::screenUpVector(float angle) const {
    const glm::quat q = camera_.orientation();
    return q * glm::vec3(-std::sin(angle), std::cos(angle), 0.0f);
}

void App::pollAlphaFile() {
    std::string alphaPath;
    {
        std::lock_guard lock(alphaMutex_);
        alphaPath.swap(pendingAlphaPath_);
    }
    if (!alphaPath.empty()) runCommand("brush_set_alpha", {{"path", alphaPath}, {"invert", alphaInvert_}});
}

// Stroke palette (and the stroke picker's settings): how dabs follow the cursor.
void App::drawStrokeSection() {
    static const char* const kHelp[] = {
        "Even dabs along the path.",
        "Click and drag one stamp: the drag's length is the size, its direction the rotation.",
        "Scattered dabs of random size and strength.",
        "The brush trails the cursor on a string, for smooth lines.",
    };
    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    for (int i = 0; i < int(StrokeType::Count); ++i) {
        const bool on = strokeType_ == StrokeType(i);
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_SliderGrab]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_SliderGrabActive]);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        }
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button((std::string(strokeTypeName(StrokeType(i))) + "##stroke").c_str(), ImVec2(half, 0)))
            strokeType_ = StrokeType(i);
        if (on) ImGui::PopStyleColor(3);
        ui::HelpText(strokeTypeName(StrokeType(i)), nullptr, kHelp[i < int(std::size(kHelp)) ? i : 0], "Stroke");
    }
    if (strokeType_ == StrokeType::Spray) {
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##scatter", &sprayScatter_, 0.0f, 2.0f, "Scatter %.2f");
        ui::HelpText("Scatter", nullptr, "How far spray dabs jump from the path, as a fraction of the brush size.", "Stroke");
    } else if (strokeType_ == StrokeType::LazyMouse) {
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##lazy", &lazyMouse_.radius, 1.0f, 300.0f, "Lazy Radius %.0f px");
        ui::HelpText("Lazy Radius", nullptr, "Length of the string the brush is pulled on; longer = smoother lines.", "Stroke");
    }
}

// Alpha palette: the stamp that shapes the brush (ZBrush alpha).
void App::drawAlphaSection() {
    const BrushAlpha& alpha = doc_.brushAlpha;
    const float s = uiScale_;
    auto preview = [](ImDrawList* dl, const BrushAlpha& a, ImVec2 p0, float size, int cells) {
        const float cell = size / float(cells);
        dl->AddRectFilled(p0, ImVec2(p0.x + size, p0.y + size), IM_COL32(0, 0, 0, 255), 3.0f);
        for (int y = 0; y < cells; ++y)
            for (int x = 0; x < cells; ++x) {
                const float u = (float(x) + 0.5f) / float(cells) * 2.0f - 1.0f, v = 1.0f - (float(y) + 0.5f) / float(cells) * 2.0f;
                const int g = int(std::clamp(a.sample(u, v), 0.0f, 1.0f) * 255.0f);
                if (g < 3) continue;
                dl->AddRectFilled(ImVec2(p0.x + float(x) * cell, p0.y + float(y) * cell),
                                  ImVec2(p0.x + float(x + 1) * cell + 0.5f, p0.y + float(y + 1) * cell + 0.5f), IM_COL32(g, g, g, 255));
            }
    };
    // Large preview of the current alpha, with its name.
    const float big = std::min(128.0f * s, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - big) * 0.5f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(big, big));
    if (ImTextureID tex = thumbs_.alpha(alpha))
        ImGui::GetWindowDrawList()->AddImage(tex, p0, ImVec2(p0.x + big, p0.y + big));
    else
        preview(ImGui::GetWindowDrawList(), alpha, p0, big, 40);
    const ImVec2 nameSize = ImGui::CalcTextSize(alpha.name.c_str());
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - nameSize.x) * 0.5f);
    ImGui::TextUnformatted(alpha.name.c_str());

    // Built-in alphas as small tiles.
    static const BrushAlpha kBuiltins[] = {BrushAlpha::round(), BrushAlpha::square(), BrushAlpha::noise()};
    const float tile = 52.0f * s;
    for (int i = 0; i < 3; ++i) {
        const BrushAlpha& a = kBuiltins[i];
        if (i) ImGui::SameLine();
        ImGui::PushID(i);
        const ImVec2 t0 = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton((a.name + "##alpha").c_str(), ImVec2(tile, tile)))
            runCommand("brush_set_alpha", {{"name", a.name}});
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (ImTextureID tex = thumbs_.alpha(a)) dl->AddImage(tex, t0, ImVec2(t0.x + tile, t0.y + tile));
        else preview(dl, a, t0, tile, 20);
        if (alpha.name == a.name || ImGui::IsItemHovered())
            dl->AddRect(t0, ImVec2(t0.x + tile, t0.y + tile), ImGui::GetColorU32(ImGuiCol_SliderGrab, alpha.name == a.name ? 1.0f : 0.6f),
                        3.0f, 0, 2.0f * s);
        ui::HelpText(a.name.c_str(), nullptr, "Use this built-in alpha.", "Alpha");
        ImGui::PopID();
    }
    if (ImGui::Button("Load image...##alpha", ImVec2(-1, 0)))
        SDL_ShowOpenFileDialog(onAlphaChosen, this, window_, kAlphaFilters, 2, nullptr, false);
    ui::HelpText("Load image", nullptr, "Use a grayscale image as the alpha: white = full effect, black = none.", "Alpha");
    ImGui::Checkbox("Invert loaded image", &alphaInvert_);
    ui::HelpText("Invert loaded image", nullptr, "Swap black and white when loading an image as an alpha.", "Alpha");
}

// SnakeHook: pull the region under the click along with the cursor, like Move,
// but re-centred on the pulled tip every step so it can be dragged out into horns.
void App::snakeHookTo(float x, float y) {
    const float h = canvasHeight();
    glm::vec3 origin, dir;
    canvasRay(x, y, origin, dir);
    const glm::vec3 n = camera_.forward();
    const float denom = glm::dot(dir, n);
    if (std::abs(denom) < 1e-6f) return;
    const glm::vec3 target = origin + dir * (glm::dot(grabLast_ - origin, n) / denom);

    const float pressure = lastInputPen_ ? std::clamp(penPressure_, 0.0f, 1.0f) : 1.0f;
    DabParams p;
    p.viewDir = n;
    p.radius = radiusPx_ * (pressureSize_ ? std::max(pressure, 0.05f) : 1.0f) * camera_.worldPerPixel(grabLast_, h);
    p.strength = strength_;
    p.pressure = pressureStrength_ ? pressure : 1.0f;
    p.invert = strokeInvert_;
    p.alpha = &doc_.brushAlpha;
    p.alphaUp = screenUpVector(0.0f);
    // Small steps keep the tip under the cursor and the pulled surface even.
    const glm::vec3 delta = target - grabLast_;
    const int steps = std::clamp(int(std::ceil(glm::length(delta) / (p.radius * 0.15f))), 1, 64);
    p.strokeDelta = delta / float(steps);
    for (int i = 0; i < steps; ++i) {
        p.center = grabLast_;
        doc_.sculptor.dab(doc_.mesh, doc_.bvh, BrushType::SnakeHook, p, symmetry_);
        grabLast_ += p.strokeDelta;
    }
}

// DragRect: undo the previous stamp and press a new one sized and rotated by the drag.
void App::dragRectTo(float x, float y) {
    doc_.sculptor.restoreStroke(doc_.mesh, doc_.bvh);
    const glm::vec2 drag = glm::vec2(x, y) - dragAnchor_;
    const float len = glm::length(drag);
    if (len < 2.0f) return;
    const glm::quat q = camera_.orientation();
    const float pressure = lastInputPen_ ? std::clamp(penPressure_, 0.0f, 1.0f) : 1.0f;

    DabParams p;
    p.center = dragAnchorHit_;
    p.viewDir = camera_.forward();
    p.radius = len * camera_.worldPerPixel(dragAnchorHit_, canvasHeight());
    p.strength = strength_;
    p.pressure = pressureStrength_ ? pressure : 1.0f;
    p.invert = strokeInvert_;
    p.frontFacesOnly = frontFacesOnly_;
    p.alpha = &doc_.brushAlpha;
    p.alphaUp = glm::normalize(q * glm::vec3(drag.x, -drag.y, 0.0f)); // the alpha's top follows the drag
    if (strokePaint_) paintAt(p); // restoreStroke reverted the previous stamp's colours too
    if (!strokeSculpt_) return;
    const BrushRecipe* recipe = strokeRecipe();
    if (recipe) doc_.sculptor.dabRecipe(doc_.mesh, doc_.bvh, *recipe, p, symmetry_);
    else doc_.sculptor.dab(doc_.mesh, doc_.bvh, strokeBrush_, p, symmetry_);
}

} // namespace sf
