// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brush options panel (stroke type, lazy mouse, spray, alpha) and the
// stroke handling of the SnakeHook brush and the DragRect stroke.
#include "app/App.h"

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

void App::drawBrushOptions() {
    std::string alphaPath;
    {
        std::lock_guard lock(alphaMutex_);
        alphaPath.swap(pendingAlphaPath_);
    }
    if (!alphaPath.empty()) runCommand("brush_set_alpha", {{"path", alphaPath}, {"invert", alphaInvert_}});

    ImGui::SeparatorText("Stroke");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##stroke", strokeTypeName(strokeType_))) {
        for (int i = 0; i < int(StrokeType::Count); ++i)
            if (ImGui::Selectable(strokeTypeName(StrokeType(i)), strokeType_ == StrokeType(i))) strokeType_ = StrokeType(i);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Dots: even dabs along the path\nDragRect: click-drag one stamp (length = size, direction = "
                          "rotation)\nSpray: scattered dabs of random size and strength\nLazyMouse: the brush trails the "
                          "cursor for smooth lines");
    if (strokeType_ == StrokeType::Spray) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##scatter", &sprayScatter_, 0.0f, 2.0f, "scatter %.2f");
    } else if (strokeType_ == StrokeType::LazyMouse) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##lazy", &lazyMouse_.radius, 1.0f, 300.0f, "lazy radius %.0f px");
    }

    ImGui::SeparatorText("Alpha");
    const BrushAlpha& alpha = doc_.brushAlpha;
    const float preview = ImGui::GetFrameHeight() * 2.0f;
    {
        // Thumbnail of the stamp.
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        constexpr int kCells = 20;
        const float cell = preview / kCells;
        for (int y = 0; y < kCells; ++y)
            for (int x = 0; x < kCells; ++x) {
                const float u = (float(x) + 0.5f) / kCells * 2.0f - 1.0f, v = 1.0f - (float(y) + 0.5f) / kCells * 2.0f;
                const int g = int(std::clamp(alpha.sample(u, v), 0.0f, 1.0f) * 255.0f);
                dl->AddRectFilled(ImVec2(p0.x + x * cell, p0.y + y * cell), ImVec2(p0.x + (x + 1) * cell, p0.y + (y + 1) * cell),
                                  IM_COL32(g, g, g, 255));
            }
        ImGui::Dummy(ImVec2(preview, preview));
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##alpha", alpha.name.c_str())) {
        for (const char* name : {"Round", "Square", "Noise"})
            if (ImGui::Selectable(name, alpha.name == name)) runCommand("brush_set_alpha", {{"name", std::string(name)}});
        ImGui::EndCombo();
    }
    if (ImGui::Button("Load image...", ImVec2(-1, 0)))
        SDL_ShowOpenFileDialog(onAlphaChosen, this, window_, kAlphaFilters, 2, nullptr, false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Grayscale image: white = full effect, black = none");
    ImGui::EndGroup();
    ImGui::Checkbox("Invert loaded image", &alphaInvert_);
}

// SnakeHook: pull the region under the click along with the cursor, like Move,
// but re-centred on the pulled tip every step so it can be dragged out into horns.
void App::snakeHookTo(float x, float y) {
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    glm::vec3 origin, dir;
    camera_.screenRay(x, y, float(w), float(h), origin, dir);
    const glm::vec3 n = camera_.forward();
    const float denom = glm::dot(dir, n);
    if (std::abs(denom) < 1e-6f) return;
    const glm::vec3 target = origin + dir * (glm::dot(grabLast_ - origin, n) / denom);

    const float pressure = lastInputPen_ ? std::clamp(penPressure_, 0.0f, 1.0f) : 1.0f;
    DabParams p;
    p.viewDir = n;
    p.radius = radiusPx_ * (pressureSize_ ? std::max(pressure, 0.05f) : 1.0f) * camera_.worldPerPixel(grabLast_, float(h));
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
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    const glm::quat q = camera_.orientation();
    const float pressure = lastInputPen_ ? std::clamp(penPressure_, 0.0f, 1.0f) : 1.0f;

    DabParams p;
    p.center = dragAnchorHit_;
    p.viewDir = camera_.forward();
    p.radius = len * camera_.worldPerPixel(dragAnchorHit_, float(h));
    p.strength = strength_;
    p.pressure = pressureStrength_ ? pressure : 1.0f;
    p.invert = strokeInvert_;
    p.frontFacesOnly = frontFacesOnly_;
    p.alpha = &doc_.brushAlpha;
    p.alphaUp = glm::normalize(q * glm::vec3(drag.x, -drag.y, 0.0f)); // the alpha's top follows the drag
    const BrushRecipe* recipe = strokeBrush_ == BrushType::Smooth ? nullptr : activeRecipe();
    if (recipe) doc_.sculptor.dabRecipe(doc_.mesh, doc_.bvh, *recipe, p, symmetry_);
    else doc_.sculptor.dab(doc_.mesh, doc_.bvh, strokeBrush_, p, symmetry_);
}

} // namespace sf
